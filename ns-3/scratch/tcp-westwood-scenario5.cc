/*
 * Scenario 5: Adaptive Tau Effectiveness
 * Dynamic network conditions.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario5");

void ChangeDelay(Ptr<Channel> channel, std::string delay) {
  DynamicCast<PointToPointChannel>(channel)->SetAttribute("Delay",
                                                          StringValue(delay));
}

int main(int argc, char *argv[]) {
  std::string transportProt = "TcpWestwood";

  // Set TCP Westwood as the congestion control algorithm
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
  Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                     TypeIdValue(TcpWestwood::GetTypeId()));
  Config::SetDefault("ns3::TcpWestwood::MinTau", TimeValue(MilliSeconds(50)));
  Config::SetDefault("ns3::TcpWestwood::MaxTau", TimeValue(MilliSeconds(2000)));

  NodeContainer nodes;
  nodes.Create(2);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("20ms"));

  NetDeviceContainer devices = p2p.Install(nodes);

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute("ErrorRate", DoubleValue(0.01)); // 1% loss
  em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
  devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Applications
  uint16_t port = 8080;
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApps = sink.Install(nodes.Get(1));
  sinkApps.Start(Seconds(0.0));
  sinkApps.Stop(Seconds(180.0));

  OnOffHelper client("ns3::TcpSocketFactory",
                     InetSocketAddress(interfaces.GetAddress(1), port));
  client.SetAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
  client.SetAttribute("PacketSize", UintegerValue(1448));
  ApplicationContainer clientApps = client.Install(nodes.Get(0));
  clientApps.Start(Seconds(1.0));
  clientApps.Stop(Seconds(180.0));

  // Dynamic changes
  // Be careful: changing channel delay at runtime in NS-3 might be tricky for
  // in-flight packets, but PointToPointChannel::SetAttribute usually works for
  // *next* packet transmission.

  Simulator::Schedule(Seconds(60.0), &ChangeDelay, devices.Get(0)->GetChannel(),
                      "100ms");
  Simulator::Schedule(Seconds(120.0), &ChangeDelay,
                      devices.Get(0)->GetChannel(), "200ms");

  // Trace Tau
  // Connect after socket creation (App starts at 1.0s)
  Simulator::Schedule(
      Seconds(1.1), +[]() {
        Ptr<Node> node = NodeList::GetNode(0);
        Ptr<TcpL4Protocol> tcp = node->GetObject<TcpL4Protocol>();
        ObjectVectorValue socketVec;
        tcp->GetAttribute("SocketList", socketVec);

        NS_LOG_UNCOND("SocketList size: " << socketVec.GetN());

        for (uint32_t i = 0; i < socketVec.GetN(); ++i) {
          Ptr<Object> socketObj = socketVec.Get(i);
          Ptr<TcpSocketBase> socket = DynamicCast<TcpSocketBase>(socketObj);

          if (socket) {
            PointerValue ptr;
            socket->GetAttribute("CongestionOps", ptr);
            Ptr<TcpWestwood> west =
                DynamicCast<TcpWestwood>(ptr.Get<TcpCongestionOps>());

            if (west) {
              NS_LOG_UNCOND("Found TcpWestwood on socket " << i);
              west->TraceConnectWithoutContext(
                  "CurrentTau", MakeCallback(+[](Time oldVal, Time newVal) {
                    std::cout << Simulator::Now().GetSeconds() << ","
                              << newVal.GetMilliSeconds() << std::endl;
                  }));
            } else {
              NS_LOG_UNCOND("Socket "
                            << i << " has CongestionOps but not TcpWestwood.");
            }
          }
        }
      });

  double simulationTime = 180.0;
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
