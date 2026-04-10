/*
 * Scenario 6: Congestion Window (cwnd) Dynamics Over Time
 *
 * Traces the cwnd evolution for TCP NewReno, TCP Westwood (Base), and
 * TCP Westwood (Adaptive) under a fixed 1% wireless loss rate.
 *
 * This shows the recovery speed after loss events:
 * - NewReno: halves cwnd on every loss (slow recovery)
 * - Westwood: sets cwnd = BWE * RTTmin (faster, smarter recovery)
 *
 * Output: time,cwnd written to scenario6_cwnd_*.csv (one file per protocol)
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-westwood.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodScenario6");

// Global output file
std::ofstream g_cwndFile;

static void CwndTracer(uint32_t oldCwnd, uint32_t newCwnd) {
  g_cwndFile << Simulator::Now().GetSeconds() << "," << newCwnd << "\n";
}

static void ConnectCwndTrace(std::string protocol) {
  // Connect to the cwnd trace of the first socket on node 0
  Config::ConnectWithoutContext(
      "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
      MakeCallback(&CwndTracer));
}

int main(int argc, char *argv[]) {
  double errorRate = 0.01;
  std::string transportProt = "TcpWestwood";
  bool adaptive = true;
  double simulationTime = 120.0;
  std::string outputDir = "scratch/tcp-westwood-project/results";

  CommandLine cmd(__FILE__);
  cmd.AddValue("errorRate", "Packet error rate on wireless link", errorRate);
  cmd.AddValue("transportProt", "TcpNewReno or TcpWestwood", transportProt);
  cmd.AddValue("adaptive", "Enable Adaptive Tau", adaptive);
  cmd.Parse(argc, argv);

  // Configure protocol
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

  // Topology: n0 (src) --(10Mbps,45ms)-- n1(BS) --(2Mbps,0.01ms,loss)-- n2(dst)
  NodeContainer nodes;
  nodes.Create(3);

  PointToPointHelper wiredP2p;
  wiredP2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  wiredP2p.SetChannelAttribute("Delay", StringValue("45ms"));

  PointToPointHelper wirelessP2p;
  wirelessP2p.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
  wirelessP2p.SetChannelAttribute("Delay", StringValue("0.01ms"));

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute("ErrorRate", DoubleValue(errorRate));
  em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

  NetDeviceContainer d01 = wiredP2p.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = wirelessP2p.Install(nodes.Get(1), nodes.Get(2));
  // Error ONLY on data forward path
  d12.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  address.Assign(d01);
  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = address.Assign(d12);

  uint16_t sinkPort = 8080;
  PacketSinkHelper sinkHelper(
      "ns3::TcpSocketFactory",
      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
  ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(2));
  sinkApps.Start(Seconds(0.0));
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

  // Open cwnd output file
  std::string modeStr = (transportProt == "TcpWestwood")
                            ? (adaptive ? "Adaptive" : "Base")
                            : "Base";
  std::string outFilename =
      outputDir + "/scenario6_cwnd_" + transportProt + "_" + modeStr + ".csv";
  g_cwndFile.open(outFilename);
  g_cwndFile << "Time,CwndBytes\n";

  // Connect cwnd trace after 1.1s (after app and socket are created)
  Simulator::Schedule(Seconds(1.1), &ConnectCwndTrace, transportProt);

  Simulator::Stop(Seconds(simulationTime + 5.0));
  Simulator::Run();

  g_cwndFile.close();
  Simulator::Destroy();
  return 0;
}
