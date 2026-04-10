/*
 * TCP Westwood: Wireless 802.11 (Static) Network Simulation
 *
 * Purpose: Evaluate TCP Westwood Adaptive Tau at scale across varying 
 * wireless network parameters in static WiFi Ad-Hoc topology:
 * - Number of nodes: 20, 40, 60, 80, 100
 * - Number of flows: 10, 20, 30, 40, 50  
 * - Packet rate: 100, 200, 300, 400, 500 pps
 *
 * Topology: Grid-based static placement (no mobility)
 * Traffic: UDP CBR (OnOff application)
 * Metrics: Throughput, E2E Delay, PDR, Packet Drop Ratio, Energy
 *
 * Integration: Validates TCP Westwood Adaptive Tau from Scenarios 1-6
 * at production scale with competing flows in real 802.11b channels.
 * Includes TcpNewReno as baseline for comparison.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/tcp-westwood.h"
#include "ns3/wifi-module.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;
using namespace ns3::energy;

NS_LOG_COMPONENT_DEFINE("WirelessWifi802p11Static");

namespace {
constexpr uint16_t kPortBase = 9000;
constexpr double kAppStartTime = 1.0;
}

struct SimulationParams {
  uint32_t numNodes = 20;
  uint32_t numFlows = 10;
  uint32_t packetRate = 100;  // packets per second
  uint32_t packetSize = 1024; // bytes
  double simTime = 100.0;     // seconds
  std::string tcpType = "TcpWestwood";
  bool adaptiveTau = true;
  std::string outputDir = "scratch/tcp-westwood-project/results";
};

struct NetworkMetrics {
  double throughput = 0.0;       // Mbps
  double e2eDelay = 0.0;         // ms (mean)
  double stdDevDelay = 0.0;      // ms (std dev)
  double pdr = 0.0;              // Packet Delivery Ratio [0-1]
  double dropRatio = 0.0;        // Packet Drop Ratio [0-1]
  double energyConsumed = 0.0;   // Joules
  uint64_t totalPacketsRx = 0;
  uint64_t totalPacketsTx = 0;
};

// Setup WiFi infrastructure with grid topology
void SetupWiFi(NodeContainer& nodes, uint32_t numNodes) {
  // WiFi Channel: Friis path loss with AWGN
  YansWifiChannelHelper channel;
  channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  channel.AddPropagationLoss("ns3::FriisPropagationLossModel");

  YansWifiPhyHelper phy;
  phy.SetChannel(channel.Create());
  phy.SetErrorRateModel("ns3::YansErrorRateModel");

  // 802.11b Configuration
  WifiMacHelper mac;
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211b);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue("DsssRate11Mbps"),
                                "ControlMode", StringValue("DsssRate1Mbps"));

  // Ad-Hoc network (no AP)
  mac.SetType("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

  // Grid-based static positioning
  // Grid dimensions: 10x10 grid with 50m spacing = ~500m x 500m deployment
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(50.0),
                                "DeltaY", DoubleValue(50.0),
                                "GridWidth", UintegerValue(10),
                                "LayoutType", StringValue("RowFirst"));

  // Static nodes (no movement)
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // Internet protocol stack
  InternetStackHelper internet;
  internet.Install(nodes);

  // IPv4 addressing
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i = ipv4.Assign(devices);

  // Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}

// Setup energy model for all nodes
EnergySourceContainer SetupEnergyModel(NodeContainer& nodes) {
  // Battery source: 100 J per node
  BasicEnergySourceHelper energySourceHelper;
  energySourceHelper.Set("BasicEnergySourceInitialEnergyJ",
                         DoubleValue(100.0));
  EnergySourceContainer sources = energySourceHelper.Install(nodes);

  // WiFi radio energy model
  WifiRadioEnergyModelHelper radioEnergyHelper;
  radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.17));  // TX: 170 mA
  radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.019)); // RX: 19 mA
  radioEnergyHelper.Set("IdleCurrentA", DoubleValue(0.006)); // Idle: 6 mA

  // Install energy model on all wifi devices
  for (uint32_t i = 0; i < nodes.GetN(); ++i) {
    Ptr<NetDevice> device = nodes.Get(i)->GetDevice(0);
    if (device != nullptr) {
      radioEnergyHelper.Install(device, sources.Get(i));
    }
  }

  return sources;
}

// Create UDP traffic flows between random source-destination pairs
void CreateTrafficFlows(NodeContainer& nodes, uint32_t numFlows,
                        uint32_t packetRate, uint32_t packetSize,
                        double simTime) {
  // Ensure numFlows doesn't exceed feasible pairs
  uint32_t maxFlows = (nodes.GetN() * (nodes.GetN() - 1)) / 2;
  if (numFlows > maxFlows) {
    NS_LOG_WARN("numFlows exceeds max pairs; capping to " << maxFlows);
    numFlows = maxFlows;
  }

  // Create deterministic source-dest pairs (avoid self-flows)
  for (uint32_t i = 0; i < numFlows; ++i) {
    uint32_t src = i % nodes.GetN();
    uint32_t dst = (i + 1 + i / nodes.GetN()) % nodes.GetN();

    // Avoid self-flows
    if (src == dst) {
      dst = (dst + 1) % nodes.GetN();
    }

    Ptr<Node> srcNode = nodes.Get(src);
    Ptr<Node> dstNode = nodes.Get(dst);

    // Get IP address of destination
    Ptr<Ipv4> ipv4Dst = dstNode->GetObject<Ipv4>();
    Ipv4Address dstAddr = ipv4Dst->GetAddress(1, 0).GetLocal();

    uint16_t port = kPortBase + i;

    // OnOff application (UDP traffic)
    OnOffHelper onoff("ns3::TcpSocketFactory",
                      InetSocketAddress(dstAddr, port));
    onoff.SetConstantRate(DataRate(packetRate * packetSize * 8),
                          packetSize);

    ApplicationContainer app = onoff.Install(srcNode);
    app.Start(Seconds(kAppStartTime));  // Start after routing converges
    app.Stop(Seconds(simTime));

    // Packet sink at destination (UDP)
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(dstAddr, port));
    ApplicationContainer sinkApp = sink.Install(dstNode);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));
  }
}

// Compute aggregated metrics from FlowMonitor
NetworkMetrics ComputeMetrics(Ptr<FlowMonitor> monitor,
                              FlowMonitorHelper& flowHelper,
                              double simTime,
                              uint32_t numFlows) {
  NetworkMetrics metrics;

  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  uint64_t totalRxBytes = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalTxPackets = 0;
  uint64_t totalLostPackets = 0;
  double totalDelay = 0.0;
  uint64_t delayCount = 0;
  std::vector<double> delays;

  for (auto& flow : stats) {
    Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
    if (tuple.destinationPort < kPortBase ||
        tuple.destinationPort >= kPortBase + numFlows) {
      continue;
    }

    totalRxBytes += flow.second.rxBytes;
    totalRxPackets += flow.second.rxPackets;
    totalTxPackets += flow.second.txPackets;
    totalLostPackets += flow.second.lostPackets;
    totalDelay += flow.second.delaySum.GetSeconds() * 1000; // Convert to ms
    delayCount += flow.second.rxPackets;

    // Collect individual delays for stddev
    if (flow.second.rxPackets > 0) {
      double flowDelay =
          (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000;
      delays.push_back(flowDelay);
    }
  }

  // Throughput (Mbps)
  double activeDuration = std::max(1.0, simTime - kAppStartTime);
  if (activeDuration > 0) {
    metrics.throughput = (totalRxBytes * 8.0) / (activeDuration * 1e6);
  }

  // E2E Delay (ms)
  if (delayCount > 0) {
    metrics.e2eDelay = totalDelay / delayCount;

    // Standard deviation
    double mean = metrics.e2eDelay;
    double sumSqDiff = 0.0;
    for (double d : delays) {
      sumSqDiff += (d - mean) * (d - mean);
    }
    metrics.stdDevDelay =
        std::sqrt(sumSqDiff / delays.size());
  }

  // PDR and Drop Ratio
  if (totalTxPackets > 0) {
    metrics.pdr = static_cast<double>(totalRxPackets) / totalTxPackets;
    metrics.dropRatio = static_cast<double>(totalLostPackets) / totalTxPackets;
  }

  metrics.totalPacketsRx = totalRxPackets;
  metrics.totalPacketsTx = totalTxPackets;

  return metrics;
}

int main(int argc, char* argv[]) {
  SimulationParams params;

  CommandLine cmd(__FILE__);
  cmd.AddValue("numNodes", "Number of wireless nodes", params.numNodes);
  cmd.AddValue("numFlows", "Number of UDP flows", params.numFlows);
  cmd.AddValue("packetRate", "Packet rate (pps)", params.packetRate);
  cmd.AddValue("packetSize", "Packet size (bytes)", params.packetSize);
  cmd.AddValue("simTime", "Simulation duration (seconds)", params.simTime);
  cmd.AddValue("tcpType", "TCP type (TcpWestwood/TcpNewReno)",
               params.tcpType);
  cmd.AddValue("adaptive", "Enable Adaptive Tau for Westwood",
               params.adaptiveTau);
  cmd.AddValue("outputDir", "Output directory", params.outputDir);
  cmd.Parse(argc, argv);

  NS_LOG_INFO("=== WiFi 802.11 Static Network Simulation ===");
  NS_LOG_INFO("Nodes: " << params.numNodes);
  NS_LOG_INFO("Flows: " << params.numFlows);
  NS_LOG_INFO("Packet Rate: " << params.packetRate << " pps");
  NS_LOG_INFO("Packet Size: " << params.packetSize << " bytes");
  NS_LOG_INFO("Sim Time: " << params.simTime << " s");
  NS_LOG_INFO("TCP Type: " << params.tcpType);
  NS_LOG_INFO("Adaptive Tau: " << (params.adaptiveTau ? "Yes" : "No"));

  // Configure TCP congestion control
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  if (params.tcpType == "TcpWestwood") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpWestwood::GetTypeId()));

    if (params.adaptiveTau) {
      // Adaptive Tau settings (from Scenarios 2-5)
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(50)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(2000)));
    } else {
      // Fixed tau (base Westwood)
      Config::SetDefault("ns3::TcpWestwood::MinTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::MaxTau",
                         TimeValue(MilliSeconds(500)));
      Config::SetDefault("ns3::TcpWestwood::Tau",
                         TimeValue(MilliSeconds(500)));
    }
  } else if (params.tcpType == "TcpNewReno") {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
  }

  // Create nodes
  NodeContainer nodes;
  nodes.Create(params.numNodes);

  // Setup WiFi network
  SetupWiFi(nodes, params.numNodes);

  // Setup energy model
  EnergySourceContainer energySources = SetupEnergyModel(nodes);

  // Create traffic flows
  CreateTrafficFlows(nodes, params.numFlows, params.packetRate,
                     params.packetSize, params.simTime);

  // Install FlowMonitor for metrics
  FlowMonitorHelper flowmonHelper;
  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

  // Run simulation
  Simulator::Stop(Seconds(params.simTime + 5.0));
  Simulator::Run();

  // Collect metrics
  NetworkMetrics metrics =
      ComputeMetrics(monitor, flowmonHelper, params.simTime, params.numFlows);

  double energyConsumed = 0.0;
  for (uint32_t i = 0; i < energySources.GetN(); ++i) {
    Ptr<EnergySource> source = energySources.Get(i);
    if (source) {
      energyConsumed += source->GetInitialEnergy() - source->GetRemainingEnergy();
    }
  }
  metrics.energyConsumed = energyConsumed;

  // Output results to file
  std::string filename = params.outputDir + "/wifi-802-11-static_n" +
                         std::to_string(params.numNodes) + "_f" +
                         std::to_string(params.numFlows) + "_r" +
                         std::to_string(params.packetRate) + "_" +
                         params.tcpType +
                         (params.adaptiveTau ? "_adaptive" : "_base") + ".txt";

  std::ofstream outfile(filename);
  if (outfile.is_open()) {
    outfile << "# WiFi 802.11 Static Wireless Network Results\n";
    outfile << "# TCP Westwood Adaptive Tau Validation at Scale\n";
    outfile << "#\n";
    outfile << "NumNodes: " << params.numNodes << "\n";
    outfile << "NumFlows: " << params.numFlows << "\n";
    outfile << "PacketRate(pps): " << params.packetRate << "\n";
    outfile << "PacketSize(bytes): " << params.packetSize << "\n";
    outfile << "SimTime(s): " << params.simTime << "\n";
    outfile << "TCPType: " << params.tcpType << "\n";
    outfile << "AdaptiveTau: " << (params.adaptiveTau ? "Yes" : "No") << "\n";
    outfile << "#\n";
    outfile << "Throughput(Mbps): " << std::fixed << std::setprecision(6)
            << metrics.throughput << "\n";
    outfile << "E2EDelay(ms): " << metrics.e2eDelay << "\n";
    outfile << "StdDevDelay(ms): " << metrics.stdDevDelay << "\n";
    outfile << "PDR: " << metrics.pdr << "\n";
    outfile << "DropRatio: " << metrics.dropRatio << "\n";
    outfile << "EnergyConsumed(J): " << metrics.energyConsumed << "\n";
    outfile << "TotalPacketsRx: " << metrics.totalPacketsRx << "\n";
    outfile << "TotalPacketsTx: " << metrics.totalPacketsTx << "\n";
    outfile.close();

    std::cout << "Results saved to: " << filename << std::endl;
    std::cout << "Throughput: " << metrics.throughput << " Mbps" << std::endl;
    std::cout << "E2E Delay: " << metrics.e2eDelay << " ms" << std::endl;
    std::cout << "PDR: " << metrics.pdr << std::endl;
  } else {
    NS_LOG_ERROR("Could not open file: " << filename);
  }

  Simulator::Destroy();
  return 0;
}
