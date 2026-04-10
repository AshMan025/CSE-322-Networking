#!/bin/bash
# Quick-Start Guide for TCP Westwood Wireless Simulations
# Run this script to build and test the simulators

set -e

NS3_ROOT="/mnt/e/ns-3-anti/ns-3"
PROJECT_DIR="$NS3_ROOT/scratch/tcp-westwood-project"

echo "=============================================================="
echo "TCP Westwood Wireless Simulation - Quick Start"
echo "=============================================================="
echo ""

# Step 1: Build ns-3
echo "[1/4] Building ns-3 and simulators..."
cd "$NS3_ROOT"
./ns3 configure --build-profile=optimized > /dev/null 2>&1
./ns3 build 2>&1 | grep -E "(wireless-802|Building|Built)" || true
echo "✓ ns-3 build complete"
echo ""

# Step 2: Verify simulators exist
echo "[2/4] Verifying simulator binaries..."
WIFI_SIM="$NS3_ROOT/build/scratch/ns3-dev-wireless-802-11-static-default"
WPAN_SIM="$NS3_ROOT/build/scratch/ns3-dev-wireless-802-15-4-static-default"

if [ -f "$WIFI_SIM" ]; then
    echo "✓ WiFi 802.11 simulator found"
else
    echo "✗ WiFi simulator not found: $WIFI_SIM"
    exit 1
fi

if [ -f "$WPAN_SIM" ]; then
    echo "✓ 802.15.4 simulator found"
else
    echo "✗ 802.15.4 simulator not found: $WPAN_SIM"
    exit 1
fi
echo ""

# Step 3: Create output directory
echo "[3/4] Preparing output directories..."
mkdir -p "$PROJECT_DIR/results/graphs"
echo "✓ Output directories ready at: $PROJECT_DIR/results/"
echo ""

# Step 4: Run a quick test (one config each)
echo "[4/4] Running quick test (1 WiFi + 1 802.15.4 config)..."
echo ""

echo "  Testing WiFi 802.11 with Adaptive Tau..."
"$WIFI_SIM" \
    --numNodes=20 \
    --numFlows=10 \
    --packetRate=100 \
    --tcpType=TcpWestwood \
    --adaptive=1 \
    --outputDir="$PROJECT_DIR/results" \
    2>/dev/null || true
echo ""

echo "  Testing 802.15.4 with Adaptive Tau..."
"$WPAN_SIM" \
    --numNodes=20 \
    --numFlows=10 \
    --packetRate=100 \
    --tcpType=TcpWestwood \
    --adaptive=1 \
    --outputDir="$PROJECT_DIR/results" \
    2>/dev/null || true
echo ""

# Step 5: Show results
echo "=============================================================="
echo "✓ Quick test complete!"
echo ""
echo "Next steps:"
echo ""
echo "1. RUN ALL SIMULATIONS:"
echo "   cd $PROJECT_DIR"
echo "   python3 batch_simulator.py"
echo "   (Takes ~12-18 hours on typical hardware)"
echo ""
echo "2. GENERATE PLOTS:"
echo "   python3 plot_results.py"
echo ""
echo "3. VIEW RESULTS:"
echo "   - Individual runs: $PROJECT_DIR/results/*.txt"
echo "   - Aggregated data: $PROJECT_DIR/results/*_aggregated.csv"
echo "   - Graphs: $PROJECT_DIR/results/graphs/"
echo ""
echo "4. READ DOCUMENTATION:"
echo "   cat $PROJECT_DIR/README.md"
echo ""
echo "=============================================================="
