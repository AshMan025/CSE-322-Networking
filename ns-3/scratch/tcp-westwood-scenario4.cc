/*
 * Scenario 4: Fairness Test
 *
 * Tests fairness among N competing TCP flows sharing a bottleneck link.
 * Run with --transportProt=TcpWestwood OR --transportProt=TcpNewReno
 * to get a clean same-protocol comparison.
 *
 * The previous "Mixed" mode caused Config::Set to race and the per-node
 * socket-type trick is unreliable once stack is installed. Running
 * each protocol separately and comparing Jain's Index is cleaner.
 *
 * Topology:
 *   src0 ─┐
 *   src1 ─┤ (100Mbps, 5ms access) → router0 ─(10Mbps, 20ms)─ router1 → sinks
 *   ...   ┘
 *
 * Output: FlowId,Protocol,Throughput written to scenario4_results.csv
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario4");

int main(int argc, char *argv[]) {
  uint32_t nFlows = 5;
  std::string transportProt = "TcpWestwood";
  bool adaptive = true;
  double simulationTime = 100.0;
  std::string outputDir = "scratch/tcp-westwood-project/results";

  CommandLine cmd(__FILE__);
  cmd.AddValue("nFlows", "Number of competing flows", nFlows);
  cmd.AddValue("transportProt", "TcpWestwood or TcpNewReno", transportProt);
  cmd.AddValue("adaptive", "Enable Adaptive Tau (Westwood only)", adaptive);
  cmd.Parse(argc, argv);

  // Set ONE congestion control globally before any stack is installed.
  // This ensures all flows use the same protocol — a prerequisite for a
  // meaningful Jain's Fairness Index measurement.
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  if (transportProt == "TcpWestwood") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpWestwood::GetTypeId()));
    if (adaptive) {
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(50)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(2000)));
    } else {
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::Tau", TimeValue(MilliSeconds(500)));
    }
  } else {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
  }

  // Increase TCP buffer to avoid being a bottleneck
  Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                     UintegerValue(1 << 21)); // 2MB
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 21));

  // Build topology: nFlows sources → router0 ─(bottleneck)─ router1 → nFlows
  // sinks
  NodeContainer routers;
  routers.Create(2); // router0, router1

  NodeContainer sources, sinks;
  sources.Create(nFlows);
  sinks.Create(nFlows);

  InternetStackHelper stack;
  stack.Install(routers);
  stack.Install(sources);
  stack.Install(sinks);

  // Bottleneck link
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));
  NetDeviceContainer dRouters =
      bottleneck.Install(routers.Get(0), routers.Get(1));

  // Access links (high capacity, low delay)
  PointToPointHelper access;
  access.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  access.SetChannelAttribute("Delay", StringValue("5ms"));

  Ipv4AddressHelper addr;

  // Assign bottleneck address
  addr.SetBase("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer iRouters = addr.Assign(dRouters);

  // Connect each source to router0 and each sink to router1
  std::vector<Ipv4InterfaceContainer> sinkInterfaces(nFlows);
  for (uint32_t i = 0; i < nFlows; ++i) {
    std::ostringstream s1, s2;

    // source[i] -- router0
    s1 << "10.1." << i << ".0";
    addr.SetBase(s1.str().c_str(), "255.255.255.0");
    NetDeviceContainer dSrc = access.Install(sources.Get(i), routers.Get(0));
    addr.Assign(dSrc);

    // router1 -- sink[i]
    s2 << "10.2." << i << ".0";
    addr.SetBase(s2.str().c_str(), "255.255.255.0");
    NetDeviceContainer dSink = access.Install(routers.Get(1), sinks.Get(i));
    sinkInterfaces[i] = addr.Assign(dSink);
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Install applications: one BulkSend per source → matching sink
  uint16_t basePort = 9000;
  std::vector<ApplicationContainer> sinkApps(nFlows);

  for (uint32_t i = 0; i < nFlows; ++i) {
    uint16_t port = basePort + i;
    // Sink
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    sinkApps[i] = sinkHelper.Install(sinks.Get(i));
    sinkApps[i].Start(Seconds(0.0));
    sinkApps[i].Stop(Seconds(simulationTime));

    // Source: saturating sender via OnOff (always on)
    OnOffHelper src("ns3::TcpSocketFactory",
                    InetSocketAddress(sinkInterfaces[i].GetAddress(1), port));
    src.SetAttribute("OnTime",
                     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    src.SetAttribute("OffTime",
                     StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    src.SetAttribute("DataRate", DataRateValue(DataRate("50Mbps")));
    src.SetAttribute("PacketSize", UintegerValue(1448));

    ApplicationContainer srcApp = src.Install(sources.Get(i));
    // Stagger starts slightly to avoid synchronized slow-starts
    srcApp.Start(Seconds(1.0 + i * 0.2));
    srcApp.Stop(Seconds(simulationTime));
  }

  // FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();

  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  // Write results: only flows with meaningful throughput (TCP data flows)
  std::ofstream out(outputDir + "/scenario4_results.csv", std::ios::app);
  out << "FlowId,Protocol,Mode,Throughput\n";

  std::string mode = (transportProt == "TcpWestwood")
                         ? (adaptive ? "Adaptive" : "Base")
                         : "Base";

  uint32_t flowIdx = 0;
  for (auto &kv : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
    // Only count sender→receiver flows (port >= 9000 = our app flows)
    if (t.destinationPort >= basePort &&
        t.destinationPort < basePort + nFlows) {
      double tput = kv.second.rxBytes * 8.0 / (simulationTime * 1e6);
      out << flowIdx++ << "," << transportProt << "," << mode << "," << tput
          << "\n";
    }
  }
  out.close();

  Simulator::Destroy();
  return 0;
}
