# TCP Westwood Wireless Network Simulations

## Overview

This directory contains a comprehensive simulation suite validating **TCP Westwood with Adaptive Tau enhancement** in large-scale wireless networks. The work extends previous research (Scenarios 1-6) from controlled point-to-point tests to production-scale deployments with competing flows.

## Project Structure

```
tcp-westwood-project/
├── wireless-802-11-static.cc          # WiFi 802.11 simulator
├── wireless-802-15-4-static.cc        # 802.15.4 LR-WPAN simulator
├── batch_simulator.py                 # Batch execution of all parameter combos
├── plot_results.py                    # Results visualization
├── results/                           # Simulation output files
│   ├── wifi-802-11-static_*.txt       # Individual WiFi runs
│   ├── 802-15-4-static_*.txt          # Individual 802.15.4 runs
│   ├── wifi_aggregated.csv            # Aggregated WiFi results
│   ├── wpan_aggregated.csv            # Aggregated 802.15.4 results
│   └── graphs/                        # Generated plots
├── research_paper.tex                 # Main research paper (with new Section 6)
└── README.md                          # This file
```

## Integration with Previous Work

### Previous Scenarios (1-6)
These scenarios validated TCP Westwood Adaptive Tau algorithm on **controlled topologies**:
- **Scenario 1**: Wireless loss resilience (throughput vs error rate)
- **Scenario 2**: RTT variance validation (adaptive tau response)
- **Scenario 3**: Burst error recovery
- **Scenario 4**: Fairness analysis (Jain's Index)
- **Scenario 5**: Real-time tau adaptation tracing
- **Scenario 6**: Congestion window behavior

### New Work: Production-Scale Validation
These simulations scale Westwood to **real-world deployments**:
- **Multiple competing flows** (10-50 simultaneous)
- **Larger networks** (20-100 nodes)
- **Higher load** (100-500 pps per flow)
- **Real wireless channels** (802.11b, 802.15.4)
- **Network-wide metrics** (PDR, energy consumption)

## Simulation Parameters

### Network Configurations
Both simulators support the same parameter variations:

| Parameter | Values |
|-----------|--------|
| Number of Nodes | 20, 40, 60, 80, 100 |
| Number of Flows | 10, 20, 30, 40, 50 |
| Packet Rate | 100, 200, 300, 400, 500 pps |
| Simulation Duration | 100 seconds |

### TCP Configurations
- **TcpWestwood** with Adaptive Tau (MinTau=50ms, MaxTau=2000ms)
- **TcpWestwood** Base (fixed Tau=500ms)
- **TcpNewReno** (baseline for comparison)

Total combinations: **(5 nodes × 5 flows × 5 rates) × 3 configs = 375 runs per network**

## Building the Simulators

The simulators are built as part of the ns-3 build system. Ensure you have:

1. Built ns-3 in the workspace:
```bash
cd ~/Desktop/3-2/Net-Sessional/ns-3-anti/ns-3
./ns3 configure --build-profile=optimized
./ns3 build
```

2. Compiled the wireless simulators:
```bash
./ns3 build | grep wireless-802
```

You should see:
```
Built ns3-dev-wireless-802-11-static-default
Built ns3-dev-wireless-802-15-4-static-default
```

## Running Simulations

### Option 1: Single Simulation
```bash
cd ns-3/build/scratch
./ns3-dev-wireless-802-11-static-default \
    --numNodes=20 \
    --numFlows=10 \
    --packetRate=100 \
    --tcpType=TcpWestwood \
    --adaptive=1 \
    --outputDir=../tcp-westwood-project/results
```

### Option 2: Batch Execution (All Combinations)
```bash
cd tcp-westwood-project
python3 batch_simulator.py
```

This runs ~750 simulations and aggregates results to CSV files.

**Note**: Full suite takes 12-18 hours on typical hardware.

### Option 3: Quick Test (Small Subset)
```bash
# Test just one configuration per network
python3 batch_simulator.py --quick
```

## Metrics Collected

### Primary Metrics
1. **Network Throughput** (Mbps for WiFi, kbps for 802.15.4)
   - Aggregate bytes received / simulation time
   
2. **End-to-End Delay** (milliseconds)
   - Mean packet latency from source to destination
   - Includes standard deviation
   
3. **Packet Delivery Ratio (PDR)** (0-1)
   - Formula: Total packets delivered / Total packets sent
   
4. **Packet Drop Ratio** (0-1)
   - Formula: Total packets dropped / Total packets sent
   
5. **Energy Consumption** (Joules)
   - Battery depletion from WiFi radio or 802.15.4 transceiver

### Secondary Metrics
- Number of flows converged
- Number of lost packets
- Standard deviation of delay

## Visualization

After running simulations, generate plots:
```bash
python3 plot_results.py
```

This creates publication-quality graphs showing:
- **Throughput trends** across nodes, flows, and packet rates
- **Delay analysis** for each parameter variation
- **PDR/Drop Ratio** comparisons between TCP variants
- **Side-by-side comparisons** of WiFi vs 802.15.4

All graphs saved to `results/graphs/`

## Key Research Findings

### WiFi 802.11 Static Results
- TCP Westwood with Adaptive Tau maintains **higher throughput** than NewReno as scale increases
- Adaptive tau better handles **interference** in larger deployments
- Energy consumption grows linearly with number of active flows

### 802.15.4 LR-WPAN Results
- Constrained bandwidth (250 kbps) challenges all TCP variants
- Adaptive Tau shows **superior PDR** in 802.15.4 due to better RTT tracking
- Energy-critical operation benefits from Westwood's bandwidth awareness

## File Formats

### Individual Result Files
```
wifi-802-11-static_n<nodes>_f<flows>_r<rate>_<tcptype>_<mode>.txt
```

Example output:
```
# WiFi 802.11 Static Wireless Network Results
NumNodes: 40
NumFlows: 20
PacketRate(pps): 200
TCPType: TcpWestwood
AdaptiveTau: Yes

Throughput(Mbps): 8.256149
E2EDelay(ms): 45.123
PDR: 0.987
DropRatio: 0.013
```

### Aggregated CSV Format
```csv
Network,NumNodes,NumFlows,PacketRate(pps),TCPType,AdaptiveTau,Throughput(Mbps),E2EDelay(ms),PDR,DropRatio
WiFi-802.11,20,10,100,TcpWestwood,Yes,9.145,23.456,0.992,0.008
...
```

## Dependencies

- **ns-3.39+** with modules: core, internet, wifi, lr-wpan, energy, flow-monitor
- **Python 3.7+** with: numpy, matplotlib, csv (standard library)

## Troubleshooting

### Simulator not found
Ensure ns-3 is built:
```bash
cd ns-3 && ./ns3 build
```

### Permission denied on Python scripts
```bash
chmod +x batch_simulator.py plot_results.py
```

### Out of memory on large runs
Reduce `NUM_NODES` or `SIM_TIME` in batch_simulator.py

### Empty CSV files
Check that simulators are producing output files in `results/` directory

## References

- **TCP Westwood paper**: Casetti et al., "TCP Westwood: End-to-end congestion control for wired/wireless networks", Wireless Networks, vol. 8, 2002.
- **Adaptive Tau enhancement**: Documented in `research_paper.tex` Sections 3-5
- **ns-3 documentation**: https://www.nsnam.org/

## Author Notes

This work builds upon 6 previous scenarios validating TCP Westwood's core algorithm. The new large-scale wireless simulations:

1. **Validate scalability** - Shows Adaptive Tau works beyond 3-5 node labs
2. **Test real channels** - Uses actual 802.11b and 802.15.4 propagation models
3. **Measure network metrics** - Moves beyond TCP-specific traces to overall network health
4. **Support production deployment** - Larger node counts and flow counts reflect real network sizes

The integration maintains backward compatibility with previous work while extending the research frontier.
