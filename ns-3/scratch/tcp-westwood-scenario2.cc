/*
 * Scenario 2: RTT Variance Test (Validate Adaptive Tau)
 * Tests performance under different RTT conditions to show where adaptive tau
 * helps.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario2");

int main(int argc, char *argv[]) {
  double errorRate = 0.01; // 1% loss
  std::string transportProt = "TcpWestwood";
  bool adaptive = true;
  std::string outputDir = "scratch/tcp-westwood-project/results";
  std::string delay = "45ms"; // Default wired delay
  double simulationTime = 200.0;

  CommandLine cmd(__FILE__);
  cmd.AddValue("delay", "Wired link delay", delay);
  cmd.AddValue("errorRate", "Packet error rate", errorRate);
  cmd.AddValue("transportProt", "Transport Protocol (TcpWestwood, TcpNewReno)",
               transportProt);
  cmd.AddValue("adaptive", "Enable Adaptive Tau", adaptive);
  cmd.Parse(argc, argv);

  // Set the transport protocol
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  if (transportProt == "TcpWestwood") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpWestwood::GetTypeId()));

    if (!adaptive) {
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

  // Topology
  NodeContainer nodes;
  nodes.Create(3);

  PointToPointHelper wiredP2p;
  wiredP2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  wiredP2p.SetChannelAttribute("Delay", StringValue(delay));

  PointToPointHelper wirelessP2p;
  wirelessP2p.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
  wirelessP2p.SetChannelAttribute("Delay", StringValue("0.01ms"));

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute("ErrorRate", DoubleValue(errorRate));
  em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

  NetDeviceContainer d01 = wiredP2p.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = wirelessP2p.Install(nodes.Get(1), nodes.Get(2));

  // Apply error model ONLY on the forward data path: n1 -> n2.
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
  client.SetAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
  client.SetAttribute("PacketSize", UintegerValue(1448));

  ApplicationContainer clientApps = client.Install(nodes.Get(0));
  clientApps.Start(Seconds(1.0));
  clientApps.Stop(Seconds(simulationTime));

  // Tracing
  wiredP2p.EnableAsciiAll(outputDir + "/scenario2-" +
                          (adaptive ? "adaptive" : "base") + ".tr");
  wiredP2p.EnablePcapAll(outputDir + "/scenario2-" +
                         (adaptive ? "adaptive" : "base"));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();

  Ptr<PacketSink> s = DynamicCast<PacketSink>(sinkApps.Get(0));
  double throughput = (s->GetTotalRx() * 8.0) / (simulationTime * 1e6);

  std::cout << delay << "," << transportProt << ","
            << (adaptive ? "Adaptive" : "Base") << "," << throughput
            << std::endl;

  Simulator::Destroy();
  return 0;
}
