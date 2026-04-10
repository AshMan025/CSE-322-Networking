/*
 * TCP Westwood: Wireless 802.11 Mobile Network Simulation
 *
 * Assignment-aligned bulk simulator for the required mobile 802.11 network.
 * One AP stays fixed at the center of the area while the remaining nodes act
 * as mobile stations and generate TCP flows toward the AP.
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

NS_LOG_COMPONENT_DEFINE("TcpWestwoodWifi80211Mobile");

namespace {

constexpr uint16_t kPortBase = 9000;
constexpr double kAppStartTime = 1.0;
constexpr double kAreaSize = 300.0;

struct SimulationParams
{
    uint32_t numNodes = 20;
    uint32_t numFlows = 10;
    uint32_t packetRate = 100;
    uint32_t packetSize = 1024;
    double simTime = 60.0;
    double nodeSpeed = 15.0;
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

EnergySourceContainer
InstallEnergyModel(NodeContainer& allNodes, NetDeviceContainer& allDevices)
{
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(100.0));
    EnergySourceContainer sources = energySourceHelper.Install(allNodes);

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.17));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.019));
    radioEnergyHelper.Set("IdleCurrentA", DoubleValue(0.006));

    for (uint32_t i = 0; i < allDevices.GetN(); ++i)
    {
        radioEnergyHelper.Install(allDevices.Get(i), sources.Get(i));
    }
    return sources;
}

Ipv4InterfaceContainer
SetupWifiNetwork(const NodeContainer& apNode,
                 const NodeContainer& staNodes,
                 NetDeviceContainer& allDevices,
                 double nodeSpeed)
{
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.SetErrorRateModel("ns3::YansErrorRateModel");

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate11Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate1Mbps"));

    WifiMacHelper mac;
    Ssid ssid = Ssid("tcp-westwood-mobile");

    mac.SetType("ns3::StaWifiMac",
                "Ssid",
                SsidValue(ssid),
                "ActiveProbing",
                BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevices = wifi.Install(phy, mac, apNode);

    MobilityHelper apMobility;
    apMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNode);
    Ptr<MobilityModel> apModel = apNode.Get(0)->GetObject<MobilityModel>();
    apModel->SetPosition(Vector(kAreaSize / 2.0, kAreaSize / 2.0, 0.0));

    MobilityHelper staMobility;
    staMobility.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"),
        "Y",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));
    staMobility.SetMobilityModel(
        "ns3::RandomWalk2dMobilityModel",
        "Bounds",
        RectangleValue(Rectangle(0.0, kAreaSize, 0.0, kAreaSize)),
        "Speed",
        StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
        "Distance",
        DoubleValue(20.0));
    staMobility.Install(staNodes);

    for (uint32_t i = 0; i < staNodes.GetN(); ++i)
    {
        Ptr<RandomWalk2dMobilityModel> mobility =
            DynamicCast<RandomWalk2dMobilityModel>(staNodes.Get(i)->GetObject<MobilityModel>());
        mobility->SetAttribute("Speed",
                               StringValue("ns3::ConstantRandomVariable[Constant=" +
                                           std::to_string(nodeSpeed) + "]"));
    }

    NodeContainer allNodes;
    allNodes.Add(apNode);
    allNodes.Add(staNodes);

    allDevices = NetDeviceContainer();
    allDevices.Add(apDevices);
    allDevices.Add(staDevices);

    InternetStackHelper internet;
    internet.Install(allNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.20.0.0", "255.255.255.0");
    return ipv4.Assign(allDevices);
}

void
CreateTrafficFlows(const NodeContainer& staNodes,
                   Ptr<Node> apNode,
                   const Ipv4InterfaceContainer& interfaces,
                   uint32_t numFlows,
                   uint32_t packetRate,
                   uint32_t packetSize,
                   double simTime)
{
    Ipv4Address apAddress = interfaces.GetAddress(0);

    for (uint32_t i = 0; i < numFlows; ++i)
    {
        uint32_t srcIndex = i % staNodes.GetN();
        uint16_t port = kPortBase + i;

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(apNode);
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simTime));

        OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(apAddress, port));
        onoff.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onoff.SetConstantRate(DataRate(packetRate * packetSize * 8), packetSize);

        ApplicationContainer app = onoff.Install(staNodes.Get(srcIndex));
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
    cmd.AddValue("numNodes", "Total number of nodes including the AP", params.numNodes);
    cmd.AddValue("numFlows", "Number of TCP flows", params.numFlows);
    cmd.AddValue("packetRate", "Packet rate (pps)", params.packetRate);
    cmd.AddValue("packetSize", "Application payload size (bytes)", params.packetSize);
    cmd.AddValue("simTime", "Simulation duration (seconds)", params.simTime);
    cmd.AddValue("nodeSpeed", "Speed of mobile stations (m/s)", params.nodeSpeed);
    cmd.AddValue("tcpType", "TCP type (TcpWestwood/TcpNewReno)", params.tcpType);
    cmd.AddValue("adaptive", "Enable Adaptive Tau for Westwood", params.adaptiveTau);
    cmd.AddValue("outputDir", "Output directory", params.outputDir);
    cmd.Parse(argc, argv);

    if (params.numNodes < 2)
    {
        NS_ABORT_MSG("WiFi mobile simulation requires at least 2 nodes");
    }

    ConfigureTcp(params);

    NodeContainer apNode;
    apNode.Create(1);

    NodeContainer staNodes;
    staNodes.Create(params.numNodes - 1);

    NetDeviceContainer allDevices;
    Ipv4InterfaceContainer interfaces = SetupWifiNetwork(apNode, staNodes, allDevices, params.nodeSpeed);

    NodeContainer allNodes;
    allNodes.Add(apNode);
    allNodes.Add(staNodes);

    EnergySourceContainer energySources = InstallEnergyModel(allNodes, allDevices);
    CreateTrafficFlows(staNodes, apNode.Get(0), interfaces, params.numFlows, params.packetRate, params.packetSize, params.simTime);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(params.simTime + 5.0));
    Simulator::Run();

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

    std::string filename = params.outputDir + "/wifi-802-11-mobile_n" +
                           std::to_string(params.numNodes) + "_f" +
                           std::to_string(params.numFlows) + "_r" +
                           std::to_string(params.packetRate) + "_s" +
                           std::to_string(static_cast<uint32_t>(params.nodeSpeed)) + "_" +
                           params.tcpType + (params.adaptiveTau ? "_adaptive" : "_base") + ".txt";

    std::ofstream outfile(filename);
    if (outfile.is_open())
    {
        outfile << "# WiFi 802.11 Mobile Network Results\n";
        outfile << "# TCP Westwood Assigned Project\n";
        outfile << "#\n";
        outfile << "NumNodes: " << params.numNodes << "\n";
        outfile << "NumFlows: " << params.numFlows << "\n";
        outfile << "PacketRate(pps): " << params.packetRate << "\n";
        outfile << "PacketSize(bytes): " << params.packetSize << "\n";
        outfile << "SimTime(s): " << params.simTime << "\n";
        outfile << "Speed(m/s): " << params.nodeSpeed << "\n";
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
