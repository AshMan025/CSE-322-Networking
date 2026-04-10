/*
 * Scenario 1: Wireless Loss Validation
 * Replicates Figure 9 from TCP Westwood paper (Throughput vs Error Rate)
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario1");

void RxTrace(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet,
             const Address &from) {
  // Simple trace to count bytes received could go here,
  // but we will use FlowMonitor or Application packet sinks for throughput.
}

int main(int argc, char *argv[]) {
  double errorRate = 0.01;
  std::string transportProt = "TcpWestwood";
  bool adaptive = false;
  std::string outputDir = "scratch/tcp-westwood-project/results";
  uint32_t payloadSize = 1448;
  double simulationTime = 200.0;

  CommandLine cmd(__FILE__);
  cmd.AddValue("errorRate", "Packet error rate (fraction)", errorRate);
  cmd.AddValue("transportProt",
               "Transport protocol to use: TcpNewReno, TcpWestwood",
               transportProt);
  cmd.AddValue("adaptive", "Enable Adaptive Tau for Westwood", adaptive);
  cmd.Parse(argc, argv);

  // Set the transport protocol
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  if (transportProt == "TcpWestwood") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpWestwood::GetTypeId()));

    // Configure Adaptive Tau
    // If adaptive is true, we strictly use the adaptive logic we implemented.
    // If adaptive is false, we should ideally disable it or use fixed tau.
    // The current implementation is ALWAYS adaptive if the logic is hardcoded
    // in AdaptTau. To strictly separate them, we might want to set
    // MinTau=MaxTau=BaseTau=500ms to disable adaptation.

    if (!adaptive) {
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::Tau", TimeValue(MilliSeconds(500)));
    } else {
      // Use defaults or tuned values for adaptive
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(50)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(2000)));
    }

  } else if (transportProt == "TcpNewReno") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
  } else if (transportProt == "TcpReno") {
    // Config::SetDefault("ns3::TcpL4Protocol::SocketType",
    // TypeIdValue(TcpReno::GetTypeId())); Note: standard Reno might not be
    // directly exposed or named TcpReno in modern NS3? TcpLinuxReno is common.
    // Let's assume standard NewReno is the comparison baseline if Reno is
    // missing. Or check if TcpLinuxReno exists.
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpLinuxReno")));
  }

  // Topology: Source --(10Mbps, 45ms)--> BaseStation --(2Mbps, 0ms, Loss)-->

  NodeContainer nodes;
  nodes.Create(3); // n0 (Source), n1 (BS), n2 (Dest)

  PointToPointHelper wiredP2p;
  wiredP2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  wiredP2p.SetChannelAttribute("Delay", StringValue("45ms"));

  PointToPointHelper wirelessP2p;
  wirelessP2p.SetDeviceAttribute(
      "DataRate", StringValue("2Mbps")); // Wireless link bandwidth
  wirelessP2p.SetChannelAttribute("Delay", StringValue("0.01ms"));

  // Install error model on wireless link (n1 -> n2)
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute("ErrorRate", DoubleValue(errorRate));
  em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

  NetDeviceContainer d01 = wiredP2p.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = wirelessP2p.Install(nodes.Get(1), nodes.Get(2));

  // Apply error model ONLY on the forward data path: n1 -> n2.
  // d12.Get(0) is n1's interface (receives from n2 = ACK path) - NO error here
  // d12.Get(1) is n2's interface (receives from n1 = DATA path) - error here
  // This matches the paper: wireless channel causes DATA packet loss,
  // but ACKs travel back on a clean path so BWE stays accurate.
  d12.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = address.Assign(d01);

  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = address.Assign(d12);

  // Install Applications
  uint16_t sinkPort = 8080;
  Address sinkAddress(InetSocketAddress(i12.GetAddress(1), sinkPort));
  PacketSinkHelper packetSinkHelper(
      "ns3::TcpSocketFactory",
      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(2));
  sinkApps.Start(Seconds(0.));
  sinkApps.Stop(Seconds(simulationTime));

  OnOffHelper client("ns3::TcpSocketFactory", sinkAddress);
  client.SetAttribute("OnTime",
                      StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  client.SetAttribute("OffTime",
                      StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  client.SetAttribute("DataRate",
                      DataRateValue(DataRate("10Mbps"))); // Saturate link
  client.SetAttribute("PacketSize", UintegerValue(payloadSize));

  ApplicationContainer clientApps = client.Install(nodes.Get(0));
  clientApps.Start(Seconds(1.0)); // warm up
  clientApps.Stop(Seconds(simulationTime));

  // Tracing
  wiredP2p.EnablePcapAll(outputDir + "/scenario1-" + transportProt);

  // Global Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();

  // Calculate throughput
  Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
  uint64_t totalBytes = sink->GetTotalRx();
  double throughput = (totalBytes * 8.0) / (simulationTime * 1000000.0); // Mbps

  std::cout << errorRate << "," << transportProt << ","
            << (adaptive ? "Adaptive" : "Base") << "," << throughput
            << std::endl;

  Simulator::Destroy();
  return 0;
}
