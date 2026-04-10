
#!/bin/bash
# Fixed version with correct sweeps and all 6 scenarios.

RESULTS="scratch/tcp-westwood-project/results"
mkdir -p "$RESULTS"
mkdir -p scratch/tcp-westwood-project/graphs

echo "Building scenarios..."
./ns3 build tcp-westwood-scenario1
./ns3 build tcp-westwood-scenario2
./ns3 build tcp-westwood-scenario3
./ns3 build tcp-westwood-scenario4
./ns3 build tcp-westwood-scenario5
./ns3 build tcp-westwood-scenario6

# -----------------------------------------------------------------------
# Scenario 1: Throughput vs Wireless Packet Error Rate
# Fix: error model now applied ONLY on data path (not ACK path)
# -----------------------------------------------------------------------



# echo "Running Scenario 1 (Wireless Loss Validation)..."
# rm -f "$RESULTS/scenario1_results.csv"
# echo "ErrorRate,Protocol,Mode,Throughput" > "$RESULTS/scenario1_results.csv"

# for err in 0.0001 0.001 0.01 0.02 0.05 0.1 0.5 0.8 0.9 0.99; do
# # print the err:
# echo $err
#     ./ns3 run "tcp-westwood-scenario1 --errorRate=$err --transportProt=TcpNewReno" \
#         >> "$RESULTS/scenario1_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario1 --errorRate=$err --transportProt=TcpWestwood --adaptive=0" \
#         >> "$RESULTS/scenario1_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario1 --errorRate=$err --transportProt=TcpWestwood --adaptive=1" \
#         >> "$RESULTS/scenario1_results.csv" 2>/dev/null
# done




# -----------------------------------------------------------------------
# Scenario 2: Throughput vs RTT (Adaptive Tau validation)
# -----------------------------------------------------------------------






# echo "Running Scenario 2 (Adaptive Tau vs RTT)..."
# rm -f "$RESULTS/scenario2_results.csv"
# echo "RTT,Protocol,Mode,Throughput" > "$RESULTS/scenario2_results.csv"

# for rtt in 1ms 5ms 10ms 15ms 20ms 30ms 50ms 100ms 200ms 250ms 300ms 350ms ; do
#     ./ns3 run "tcp-westwood-scenario2 --delay=$rtt --transportProt=TcpNewReno" \
#         >> "$RESULTS/scenario2_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario2 --delay=$rtt --transportProt=TcpWestwood --adaptive=0" \
#         >> "$RESULTS/scenario2_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario2 --delay=$rtt --transportProt=TcpWestwood --adaptive=1" \
#         >> "$RESULTS/scenario2_results.csv" 2>/dev/null
# done

# -----------------------------------------------------------------------
# Scenario 3: Burst Error Performance — SWEEP of burst rates
# Fix: error-rate sweep instead of a single data point
# -----------------------------------------------------------------------



# echo "Running Scenario 3 (Burst Errors - sweep)..."
# rm -f "$RESULTS/scenario3_results.csv"
# echo "BurstRate,Protocol,Mode,Throughput" > "$RESULTS/scenario3_results.csv"

# for burst in 0.01 0.02 0.05 0.10 0.15 0.20; do
#     ./ns3 run "tcp-westwood-scenario3 --badStateErrorRate=$burst --transportProt=TcpNewReno" \
#         >> "$RESULTS/scenario3_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario3 --badStateErrorRate=$burst --transportProt=TcpWestwood --adaptive=0" \
#         >> "$RESULTS/scenario3_results.csv" 2>/dev/null
#     ./ns3 run "tcp-westwood-scenario3 --badStateErrorRate=$burst --transportProt=TcpWestwood --adaptive=1" \
#         >> "$RESULTS/scenario3_results.csv" 2>/dev/null
# done



# -----------------------------------------------------------------------
# Scenario 4: Fairness — separate runs per protocol
# Fix: single protocol per run; results appended with protocol label
# -----------------------------------------------------------------------
# echo "Running Scenario 4 (Fairness - separate protocol runs)..."
# rm -f "$RESULTS/scenario4_results.csv"
# echo "FlowId,Protocol,Mode,Throughput" > "$RESULTS/scenario4_results.csv"

# ./ns3 run "tcp-westwood-scenario4 --nFlows=5 --transportProt=TcpNewReno" 2>/dev/null
# ./ns3 run "tcp-westwood-scenario4 --nFlows=5 --transportProt=TcpWestwood --adaptive=0" 2>/dev/null
# ./ns3 run "tcp-westwood-scenario4 --nFlows=5 --transportProt=TcpWestwood --adaptive=1" 2>/dev/null

# -----------------------------------------------------------------------
# Scenario 5: Tau Adaptation Dynamics
# -----------------------------------------------------------------------
echo "Running Scenario 5 (Tau Adaptation Effectiveness)..."
./ns3 run tcp-westwood-scenario5 \
    > "$RESULTS/scenario5_output.txt" 2>&1

# -----------------------------------------------------------------------
# Scenario 6: Congestion Window (cwnd) Over Time
# Shows Westwood's faster cwnd recovery vs NewReno's halving
# -----------------------------------------------------------------------
# echo "Running Scenario 6 (Congestion Window Dynamics)..."
# ./ns3 run "tcp-westwood-scenario6 --transportProt=TcpNewReno" 2>/dev/null
# ./ns3 run "tcp-westwood-scenario6 --transportProt=TcpWestwood --adaptive=0" 2>/dev/null
# ./ns3 run "tcp-westwood-scenario6 --transportProt=TcpWestwood --adaptive=1" 2>/dev/null






# -----------------------------------------------------------------------
# Generate all graphs
# -----------------------------------------------------------------------
echo "Generating graphs..."
python3 create_graphs.py

echo "All simulations and graph generation completed."
