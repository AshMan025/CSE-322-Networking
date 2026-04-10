/*
 * TCP Westwood: Wired CSMA Network Simulation
 *
 * Assignment-aligned bulk simulator for the required wired network.
 * The topology is a single CSMA LAN with one sink/server and multiple
 * traffic sources. Each configured flow uses TCP and targets the server.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/tcp-westwood.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpWestwoodWiredCsma");

namespace {

constexpr uint16_t kPortBase = 9000;
constexpr double kAppStartTime = 1.0;

struct SimulationParams
{
    uint32_t numNodes = 20;
    uint32_t numFlows = 10;
    uint32_t packetRate = 100;
    uint32_t packetSize = 1024;
    double simTime = 60.0;
    std::string tcpType = "TcpWestwood";
    bool adaptiveTau = true;
    std::string outputDir = "scratch/tcp-westwood-project/results";
};

struct NetworkMetrics
{
    double throughput = 0.0;
    double e2eDelay = 0.0;
    double stdDevDelay = 0.0;
    double pdr = 0.0;
    double dropRatio = 0.0;
    double energyConsumed = 0.0;
    uint64_t totalPacketsRx = 0;
    uint64_t totalPacketsTx = 0;
};

void
ConfigureTcp(const SimulationParams& params)
{
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(params.packetSize));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    if (params.tcpType == "TcpWestwood")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));
        Config::SetDefault("ns3::TcpWestwood::RecentRttWindowSize", UintegerValue(4));
        if (params.adaptiveTau)
        {
            Config::SetDefault("ns3::TcpWestwood::MinTau", TimeValue(MilliSeconds(50)));
            Config::SetDefault("ns3::TcpWestwood::MaxTau", TimeValue(MilliSeconds(2000)));
        }
        else
        {
            Config::SetDefault("ns3::TcpWestwood::MinTau", TimeValue(MilliSeconds(500)));
            Config::SetDefault("ns3::TcpWestwood::MaxTau", TimeValue(MilliSeconds(500)));
            Config::SetDefault("ns3::TcpWestwood::Tau", TimeValue(MilliSeconds(500)));
        }
    }
    else
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
    }
}

Ipv4InterfaceContainer
SetupWiredNetwork(NodeContainer& nodes)
{
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

    NetDeviceContainer devices = csma.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.10.0.0", "255.255.255.0");
    return ipv4.Assign(devices);
}

void
CreateTrafficFlows(const NodeContainer& nodes,
                   const Ipv4InterfaceContainer& interfaces,
                   uint32_t numFlows,
                   uint32_t packetRate,
                   uint32_t packetSize,
                   double simTime)
{
    if (nodes.GetN() < 2)
    {
        NS_ABORT_MSG("Wired simulation requires at least 2 nodes");
    }

    Address sinkAddressTemplate(InetSocketAddress(interfaces.GetAddress(0), kPortBase));
    (void) sinkAddressTemplate;

    for (uint32_t i = 0; i < numFlows; ++i)
    {
        uint32_t srcIndex = 1 + (i % (nodes.GetN() - 1));
        uint16_t port = kPortBase + i;

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(nodes.Get(0));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));

        OnOffHelper onoff("ns3::TcpSocketFactory",
                          InetSocketAddress(interfaces.GetAddress(0), port));
        onoff.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onoff.SetConstantRate(DataRate(packetRate * packetSize * 8), packetSize);

        ApplicationContainer app = onoff.Install(nodes.Get(srcIndex));
        app.Start(Seconds(kAppStartTime));
        app.Stop(Seconds(simTime));
    }
}

NetworkMetrics
ComputeMetrics(Ptr<FlowMonitor> monitor, FlowMonitorHelper& flowHelper, double simTime, uint32_t numFlows)
{
    NetworkMetrics metrics;
    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    uint64_t totalRxBytes = 0;
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;
    uint64_t totalLostPackets = 0;
    double totalDelayMs = 0.0;
    uint64_t delayCount = 0;
    std::vector<double> meanDelaysMs;

    for (const auto& entry : stats)
    {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(entry.first);
        if (tuple.destinationPort < kPortBase || tuple.destinationPort >= kPortBase + numFlows)
        {
            continue;
        }

        const FlowMonitor::FlowStats& flowStats = entry.second;
        totalRxBytes += flowStats.rxBytes;
        totalRxPackets += flowStats.rxPackets;
        totalTxPackets += flowStats.txPackets;
        totalLostPackets += flowStats.lostPackets;
        totalDelayMs += flowStats.delaySum.GetSeconds() * 1000.0;
        delayCount += flowStats.rxPackets;

        if (flowStats.rxPackets > 0)
        {
            double meanDelayMs =
                (flowStats.delaySum.GetSeconds() / flowStats.rxPackets) * 1000.0;
            meanDelaysMs.push_back(meanDelayMs);
        }
    }

    double activeDuration = std::max(1.0, simTime - kAppStartTime);
    metrics.throughput = (totalRxBytes * 8.0) / (activeDuration * 1e6);

    if (delayCount > 0)
    {
        metrics.e2eDelay = totalDelayMs / delayCount;

        double sumSqDiff = 0.0;
        for (double flowMeanDelay : meanDelaysMs)
        {
            sumSqDiff += (flowMeanDelay - metrics.e2eDelay) * (flowMeanDelay - metrics.e2eDelay);
        }
        if (!meanDelaysMs.empty())
        {
            metrics.stdDevDelay = std::sqrt(sumSqDiff / meanDelaysMs.size());
        }
    }

    if (totalTxPackets > 0)
    {
        metrics.pdr = static_cast<double>(totalRxPackets) / totalTxPackets;
        metrics.dropRatio = static_cast<double>(totalLostPackets) / totalTxPackets;
    }

    metrics.totalPacketsRx = totalRxPackets;
    metrics.totalPacketsTx = totalTxPackets;
    return metrics;
}

} // namespace

int
main(int argc, char* argv[])
{
    SimulationParams params;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numNodes", "Number of wired nodes", params.numNodes);
    cmd.AddValue("numFlows", "Number of TCP flows", params.numFlows);
    cmd.AddValue("packetRate", "Packet rate (pps)", params.packetRate);
    cmd.AddValue("packetSize", "Application payload size (bytes)", params.packetSize);
    cmd.AddValue("simTime", "Simulation duration (seconds)", params.simTime);
    cmd.AddValue("tcpType", "TCP type (TcpWestwood/TcpNewReno)", params.tcpType);
    cmd.AddValue("adaptive", "Enable Adaptive Tau for Westwood", params.adaptiveTau);
    cmd.AddValue("outputDir", "Output directory", params.outputDir);
    cmd.Parse(argc, argv);

    ConfigureTcp(params);

    NodeContainer nodes;
    nodes.Create(params.numNodes);

    Ipv4InterfaceContainer interfaces = SetupWiredNetwork(nodes);
    CreateTrafficFlows(nodes, interfaces, params.numFlows, params.packetRate, params.packetSize, params.simTime);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(params.simTime + 5.0));
    Simulator::Run();

    NetworkMetrics metrics = ComputeMetrics(monitor, flowmonHelper, params.simTime, params.numFlows);

    std::string filename = params.outputDir + "/wired-csma_n" + std::to_string(params.numNodes) +
                           "_f" + std::to_string(params.numFlows) + "_r" +
                           std::to_string(params.packetRate) + "_" + params.tcpType +
                           (params.adaptiveTau ? "_adaptive" : "_base") + ".txt";

    std::ofstream outfile(filename);
    if (outfile.is_open())
    {
        outfile << "# Wired CSMA Network Results\n";
        outfile << "# TCP Westwood Assigned Project\n";
        outfile << "#\n";
        outfile << "NumNodes: " << params.numNodes << "\n";
        outfile << "NumFlows: " << params.numFlows << "\n";
        outfile << "PacketRate(pps): " << params.packetRate << "\n";
        outfile << "PacketSize(bytes): " << params.packetSize << "\n";
        outfile << "SimTime(s): " << params.simTime << "\n";
        outfile << "Speed(m/s): 0\n";
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
    }
    else
    {
        NS_LOG_ERROR("Could not open file: " << filename);
    }

    Simulator::Destroy();
    return 0;
}
