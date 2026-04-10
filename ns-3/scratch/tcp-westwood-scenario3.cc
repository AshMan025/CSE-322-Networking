/*
 * Scenario 3: Burst Error Model
 * Replicate performance under bursty wireless losses.
 *
 * BurstErrorModel: with probability ErrorRate, drops a burst of consecutive
 * packets (BurstSize drawn from a random distribution). This simulates
 * a 2-state channel where the "bad" state causes bursts of consecutive
 * packet losses, as in a fading wireless channel.
 *
 * Error is applied ONLY on the forward data path (n1->n2), so ACK
 * packets are not corrupted and BWE remains accurate.
 *
 * Output: badStateErrorRate,Protocol,Mode,Throughput
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario3");

int main(int argc, char *argv[]) {
  double badStateErrorRate = 0.05;
  std::string transportProt = "TcpWestwood";
  bool adaptive = true;
  double simulationTime = 200.0;

  CommandLine cmd(__FILE__);
  cmd.AddValue("badStateErrorRate",
               "Burst error rate (probability of starting a burst)",
               badStateErrorRate);
  cmd.AddValue("transportProt", "Transport Protocol (TcpWestwood, TcpNewReno)",
               transportProt);
  cmd.AddValue("adaptive", "Enable Adaptive Tau (Westwood only)", adaptive);
  cmd.Parse(argc, argv);

  // Set the transport protocol
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  // Set congestion control
  if (transportProt == "TcpWestwood") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpWestwood::GetTypeId()));
    if (!adaptive) {
      // Fixed tau = base Westwood
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::Tau", TimeValue(MilliSeconds(500)));
    } else {
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(50)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(2000)));
    }
  } else if (transportProt == "TcpNewReno") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
  }

  // Topology: n0 --(wired 10Mbps,45ms)--> n1 --(wireless
  // 2Mbps,0.01ms,errors)--> n2
  NodeContainer nodes;
  nodes.Create(3);

  PointToPointHelper wiredP2p;
  wiredP2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  wiredP2p.SetChannelAttribute("Delay", StringValue("45ms"));

  PointToPointHelper wirelessP2p;
  wirelessP2p.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
  wirelessP2p.SetChannelAttribute("Delay", StringValue("0.01ms"));

  // BurstErrorModel: with prob=badStateErrorRate, burst of 1-4 packets is
  // dropped Average effective loss = badStateErrorRate * E[BurstSize] =
  // badStateErrorRate * 2.5
  Ptr<BurstErrorModel> em = CreateObject<BurstErrorModel>();
  em->SetAttribute("ErrorRate", DoubleValue(badStateErrorRate));
  em->SetAttribute("BurstSize",
                   StringValue("ns3::UniformRandomVariable[Min=1|Max=4]"));

  NetDeviceContainer d01 = wiredP2p.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = wirelessP2p.Install(nodes.Get(1), nodes.Get(2));

  // Apply error ONLY to forward data path: n2's receive interface (from n1)
  // d12.Get(0) = n1's device (receives ACKs from n2) -- NO error
  // d12.Get(1) = n2's device (receives data from n1) -- error applied here
  d12.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  address.Assign(d01);
  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = address.Assign(d12);

  uint16_t sinkPort = 8080;
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
  ApplicationContainer sinkApps = sink.Install(nodes.Get(2));
  sinkApps.Start(Seconds(0.));
  sinkApps.Stop(Seconds(simulationTime));

  OnOffHelper client("ns3::TcpSocketFactory",
                     InetSocketAddress(i12.GetAddress(1), sinkPort));
  client.SetAttribute("OnTime",
                      StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  client.SetAttribute("OffTime",
                      StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  client.SetAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
  client.SetAttribute("PacketSize", UintegerValue(1448));

  ApplicationContainer clientApps = client.Install(nodes.Get(0));
  clientApps.Start(Seconds(1.0));
  clientApps.Stop(Seconds(simulationTime));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();

  Ptr<PacketSink> s = DynamicCast<PacketSink>(sinkApps.Get(0));
  double throughput = (s->GetTotalRx() * 8.0) / (simulationTime * 1e6);
  std::cout << badStateErrorRate << "," << transportProt << ","
            << (adaptive ? "Adaptive" : "Base") << "," << throughput
            << std::endl;

  Simulator::Destroy();
  return 0;
}
