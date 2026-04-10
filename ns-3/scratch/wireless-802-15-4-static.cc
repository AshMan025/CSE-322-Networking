/*
 * TCP Westwood: Wireless 802.15.4 (Static) Network Simulation
 *
 * Purpose: Evaluate TCP Westwood Adaptive Tau on low-power personal area
 * networks (LPWAN) across varying network parameters:
 * - Number of nodes: 20, 40, 60, 80, 100
 * - Number of flows: 10, 20, 30, 40, 50
 * - Packet rate: 100, 200, 300, 400, 500 pps
 *
 * Topology: Grid-based static placement (LR-WPAN/Zigbee-like)
 * Traffic: UDP CBR (OnOff application)
 * Metrics: Throughput, E2E Delay, PDR, Packet Drop Ratio, Energy
 *
 * 802.15.4 Characteristics:
 * - Bandwidth: 250 kbps (vs 11 Mbps WiFi)
 * - Range: ~100m line-of-sight
 * - Low power operation
 * - High latency vs WiFi
 *
 * Integration: Validates TCP Westwood Adaptive Tau in constrained networks
 * where bandwidth and energy are critical. Baseline: TcpNewReno.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv6-flow-classifier.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/tcp-westwood.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;
using namespace ns3::energy;
using namespace ns3::lrwpan;

NS_LOG_COMPONENT_DEFINE("WirelessLrWpan802p15p4Static");

namespace {
constexpr uint16_t kPortBase = 9000;
constexpr double kAppStartTime = 1.0;
}

struct SimulationParams
{
    uint32_t numNodes = 20;
    uint32_t numFlows = 10;
    uint32_t packetRate = 100; // packets per second
    uint32_t packetSize = 100; // bytes (smaller for low-power)
    double simTime = 100.0;    // seconds
    std::string tcpType = "TcpWestwood";
    bool adaptiveTau = true;
    std::string outputDir = "scratch/tcp-westwood-project/results";
};

struct NetworkMetrics
{
    double throughput = 0.0;     // kbps (note: 802.15.4 is slower)
    double e2eDelay = 0.0;       // ms (mean)
    double stdDevDelay = 0.0;    // ms (std dev)
    double pdr = 0.0;            // Packet Delivery Ratio [0-1]
    double dropRatio = 0.0;      // Packet Drop Ratio [0-1]
    double energyConsumed = 0.0; // Joules
    uint64_t totalPacketsRx = 0;
    uint64_t totalPacketsTx = 0;
};

// Setup 802.15.4 LR-WPAN network
void
SetupLrWpan(NodeContainer& nodes, uint32_t numNodes)
{
    // Grid-based static positioning (closer spacing for lower range)
    // 802.15.4 has ~100m range, so use 20m grid spacing
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(20.0),
                                  "DeltaY", DoubleValue(20.0),
                                  "GridWidth", UintegerValue(10),
                                  "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // LR-WPAN helpers with default channel (LogDistance + ConstantSpeed)
    LrWpanHelper lrWpanHelper;
    lrWpanHelper.SetPropagationDelayModel("ns3::ConstantSpeedPropagationDelayModel");
    lrWpanHelper.AddPropagationLossModel("ns3::LogDistancePropagationLossModel");

    // Install LR-WPAN devices on all nodes
    NetDeviceContainer devices = lrWpanHelper.Install(nodes);

    // Create PAN (Personal Area Network) — all nodes join PAN 10
    lrWpanHelper.CreateAssociatedPan(devices, 10);

    // Install 6LoWPAN on top of LR-WPAN devices
    SixLowPanHelper sixlowpan;
    NetDeviceContainer six1 = sixlowpan.Install(devices);

    // Internet protocol stack (IPv6 only for 6LoWPAN)
    InternetStackHelper internet;
    internet.SetIpv4StackInstall(false);
    internet.Install(nodes);

    // IPv6 addressing
    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer i = ipv6.Assign(six1);

    // Populate routing table for IPv6
    Ipv6StaticRoutingHelper ipv6RoutingHelper;
    for (uint32_t j = 0; j < nodes.GetN(); ++j) {
        Ptr<Ipv6StaticRouting> staticRouting = ipv6RoutingHelper.GetStaticRouting(nodes.Get(j)->GetObject<Ipv6>());
        staticRouting->SetDefaultRoute(i.GetAddress(j, 1), 1);
    }
}

// Setup energy model for 802.15.4 nodes
// Note: NS-3 lacks a dedicated LrWpan radio energy model,
// so we use BasicEnergySource with estimated consumption.
// Energy is approximated as TotalEnergy = InitialEnergy - RemainingEnergy.
EnergySourceContainer
SetupEnergyModel(NodeContainer& nodes)
{
    // Battery: typical AA = 2400 mAh @ 3V ≈ 8640 J
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ",
                           DoubleValue(100.0));
    // Approximate supply voltage for 802.15.4
    energySourceHelper.Set("BasicEnergySupplyVoltageV",
                           DoubleValue(3.0));
    EnergySourceContainer sources = energySourceHelper.Install(nodes);
    return sources;
}

// Create UDP traffic flows
void
CreateTrafficFlows(NodeContainer& nodes,
                   uint32_t numFlows,
                   uint32_t packetRate,
                   uint32_t packetSize,
                   double simTime)
{
    // Ensure numFlows doesn't exceed feasible pairs
    uint32_t maxFlows = (nodes.GetN() * (nodes.GetN() - 1)) / 2;
    if (numFlows > maxFlows)
    {
        NS_LOG_WARN("numFlows exceeds max pairs; capping to " << maxFlows);
        numFlows = maxFlows;
    }

    // Create deterministic source-dest pairs
    for (uint32_t i = 0; i < numFlows; ++i)
    {
        uint32_t src = i % nodes.GetN();
        uint32_t dst = (i + 1 + i / nodes.GetN()) % nodes.GetN();

        if (src == dst)
        {
            dst = (dst + 1) % nodes.GetN();
        }

        Ptr<Node> srcNode = nodes.Get(src);
        Ptr<Node> dstNode = nodes.Get(dst);

        // Get destination IPv6 IP
        Ptr<Ipv6> ipv6Dst = dstNode->GetObject<Ipv6>();
        Ipv6Address dstAddr = ipv6Dst->GetAddress(1, 1).GetAddress();

        uint16_t port = kPortBase + i; // Unique port per flow

        // OnOff application (TCP CBR)
        OnOffHelper onoff("ns3::TcpSocketFactory", Inet6SocketAddress(dstAddr, port));
        onoff.SetConstantRate(DataRate(packetRate * packetSize * 8), packetSize);

        ApplicationContainer app = onoff.Install(srcNode);
        app.Start(Seconds(kAppStartTime));
        app.Stop(Seconds(simTime));

        // Sink
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              Inet6SocketAddress(dstAddr, port));
        ApplicationContainer sinkApp = sink.Install(dstNode);
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));
    }
}

// Compute aggregated metrics
NetworkMetrics
ComputeMetrics(Ptr<FlowMonitor> monitor, FlowMonitorHelper& flowHelper, double simTime, uint32_t numFlows)
{
    NetworkMetrics metrics;

    monitor->CheckForLostPackets();
    Ptr<Ipv6FlowClassifier> classifier =
        DynamicCast<Ipv6FlowClassifier>(flowHelper.GetClassifier6());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    uint64_t totalRxBytes = 0;
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;
    uint64_t totalLostPackets = 0;
    double totalDelay = 0.0;
    uint64_t delayCount = 0;
    std::vector<double> delays;

    for (auto& flow : stats)
    {
        Ipv6FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
        if (tuple.destinationPort < kPortBase || tuple.destinationPort >= kPortBase + numFlows)
        {
            continue;
        }

        totalRxBytes += flow.second.rxBytes;
        totalRxPackets += flow.second.rxPackets;
        totalTxPackets += flow.second.txPackets;
        totalLostPackets += flow.second.lostPackets;
        totalDelay += flow.second.delaySum.GetSeconds() * 1000;
        delayCount += flow.second.rxPackets;

        if (flow.second.rxPackets > 0)
        {
            double flowDelay = (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000;
            delays.push_back(flowDelay);
        }
    }

    // Throughput in kbps (note: 802.15.4 is slower)
    double activeDuration = std::max(1.0, simTime - kAppStartTime);
    if (activeDuration > 0)
    {
        metrics.throughput = (totalRxBytes * 8.0) / (activeDuration * 1000); // kbps
    }

    // E2E Delay
    if (delayCount > 0)
    {
        metrics.e2eDelay = totalDelay / delayCount;

        double mean = metrics.e2eDelay;
        double sumSqDiff = 0.0;
        for (double d : delays)
        {
            sumSqDiff += (d - mean) * (d - mean);
        }
        metrics.stdDevDelay = std::sqrt(sumSqDiff / delays.size());
    }

    // PDR and Drop Ratio
    if (totalTxPackets > 0)
    {
        metrics.pdr = static_cast<double>(totalRxPackets) / totalTxPackets;
        metrics.dropRatio = static_cast<double>(totalLostPackets) / totalTxPackets;
    }

    metrics.totalPacketsRx = totalRxPackets;
    metrics.totalPacketsTx = totalTxPackets;

    return metrics;
}

int
main(int argc, char* argv[])
{
    SimulationParams params;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numNodes", "Number of 802.15.4 nodes", params.numNodes);
    cmd.AddValue("numFlows", "Number of UDP flows", params.numFlows);
    cmd.AddValue("packetRate", "Packet rate (pps)", params.packetRate);
    cmd.AddValue("packetSize", "Packet size (bytes)", params.packetSize);
    cmd.AddValue("simTime", "Simulation duration (seconds)", params.simTime);
    cmd.AddValue("tcpType", "TCP type (TcpWestwood/TcpNewReno)", params.tcpType);
    cmd.AddValue("adaptive", "Enable Adaptive Tau for Westwood", params.adaptiveTau);
    cmd.AddValue("outputDir", "Output directory", params.outputDir);
    cmd.Parse(argc, argv);

    NS_LOG_INFO("=== 802.15.4 LR-WPAN Static Network Simulation ===");
    NS_LOG_INFO("Nodes: " << params.numNodes);
    NS_LOG_INFO("Flows: " << params.numFlows);
    NS_LOG_INFO("Packet Rate: " << params.packetRate << " pps");
    NS_LOG_INFO("Packet Size: " << params.packetSize << " bytes");
    NS_LOG_INFO("Sim Time: " << params.simTime << " s");
    NS_LOG_INFO("TCP Type: " << params.tcpType);
    NS_LOG_INFO("Adaptive Tau: " << (params.adaptiveTau ? "Yes" : "No"));

    // Configure TCP for LR-WPAN constraints
    // Max 802.15.4 frame size is ~127 bytes. IPv6 (40) + TCP (20) = 60.
    // That means payload must be around 60 bytes to avoid fragmentation.
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(60));

    if (params.tcpType == "TcpWestwood")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));

        if (params.adaptiveTau)
        {
            Config::SetDefault("ns3::TcpWestwood::MinTau", TimeValue(MilliSeconds(50)));
            Config::SetDefault("ns3::TcpWestwood::MaxTau", TimeValue(MilliSeconds(2000)));
        }
        else
        {
            Config::SetDefault("ns3::TcpWestwood::MinTau", TimeValue(MilliSeconds(500)));
            Config::SetDefault("ns3::TcpWestwood::MaxTau", TimeValue(MilliSeconds(500)));
        }
    }
    else if (params.tcpType == "TcpNewReno")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
    }

    // Create nodes
    NodeContainer nodes;
    nodes.Create(params.numNodes);

    // Setup 802.15.4 network
    SetupLrWpan(nodes, params.numNodes);

    // Setup energy model
    EnergySourceContainer energySources = SetupEnergyModel(nodes);

    // Create traffic flows
    CreateTrafficFlows(nodes,
                       params.numFlows,
                       params.packetRate,
                       params.packetSize,
                       params.simTime);

    // Install FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    // Run simulation
    Simulator::Stop(Seconds(params.simTime + 5.0));
    Simulator::Run();

    // Collect metrics
    NetworkMetrics metrics = ComputeMetrics(monitor, flowmonHelper, params.simTime, params.numFlows);

    double energyConsumed = 0.0;
    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        Ptr<EnergySource> source = energySources.Get(i);
        if (source)
        {
            energyConsumed += source->GetInitialEnergy() - source->GetRemainingEnergy();
        }
    }
    metrics.energyConsumed = energyConsumed;

    // Output results
    std::string filename =
        params.outputDir + "/802-15-4-static_n" + std::to_string(params.numNodes) + "_f" +
        std::to_string(params.numFlows) + "_r" + std::to_string(params.packetRate) + "_" +
        params.tcpType + (params.adaptiveTau ? "_adaptive" : "_base") + ".txt";

    std::ofstream outfile(filename);
    if (outfile.is_open())
    {
        outfile << "# 802.15.4 LR-WPAN Static Wireless Network Results\n";
        outfile << "# TCP Westwood Adaptive Tau in Constrained Networks\n";
        outfile << "#\n";
        outfile << "NumNodes: " << params.numNodes << "\n";
        outfile << "NumFlows: " << params.numFlows << "\n";
        outfile << "PacketRate(pps): " << params.packetRate << "\n";
        outfile << "PacketSize(bytes): " << params.packetSize << "\n";
        outfile << "SimTime(s): " << params.simTime << "\n";
        outfile << "TCPType: " << params.tcpType << "\n";
        outfile << "AdaptiveTau: " << (params.adaptiveTau ? "Yes" : "No") << "\n";
        outfile << "#\n";
        outfile << "Throughput(kbps): " << std::fixed << std::setprecision(6) << metrics.throughput
                << "\n";
        outfile << "E2EDelay(ms): " << metrics.e2eDelay << "\n";
        outfile << "StdDevDelay(ms): " << metrics.stdDevDelay << "\n";
        outfile << "PDR: " << metrics.pdr << "\n";
        outfile << "DropRatio: " << metrics.dropRatio << "\n";
        outfile << "EnergyConsumed(J): " << metrics.energyConsumed << "\n";
        outfile << "TotalPacketsRx: " << metrics.totalPacketsRx << "\n";
        outfile << "TotalPacketsTx: " << metrics.totalPacketsTx << "\n";
        outfile.close();

        std::cout << "Results saved to: " << filename << std::endl;
        std::cout << "Throughput: " << metrics.throughput << " kbps" << std::endl;
        std::cout << "E2E Delay: " << metrics.e2eDelay << " ms" << std::endl;
        std::cout << "PDR: " << metrics.pdr << std::endl;
    }
    else
    {
        NS_LOG_ERROR("Could not open file: " << filename);
    }

    Simulator::Destroy();
    return 0;
}
