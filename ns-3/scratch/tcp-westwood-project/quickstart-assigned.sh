#!/bin/bash
# Quick-start smoke test for assigned static wireless TCP Westwood networks.

set -e

NS3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJECT_DIR="$NS3_ROOT/scratch/tcp-westwood-project"

echo "=============================================================="
echo "TCP Westwood Assigned Simulation - Quick Start"
echo "=============================================================="
echo ""

echo "[1/4] Building ns-3 and simulators..."
cd "$NS3_ROOT"
./ns3 configure --build-profile=optimized > /dev/null 2>&1
./ns3 build wireless-802-11-static wireless-802-15-4-static 2>&1 | grep -E "(wireless-802-11-static|wireless-802-15-4-static|Building|Built)" || true
echo "Build complete"
echo ""

echo "[2/4] Verifying simulator binaries..."
WIFI_SIM="$(find "$NS3_ROOT/build/scratch" -maxdepth 1 -type f -name 'ns3-dev-wireless-802-11-static-*' | head -n 1)"
WPAN_SIM="$(find "$NS3_ROOT/build/scratch" -maxdepth 1 -type f -name 'ns3-dev-wireless-802-15-4-static-*' | head -n 1)"

if [ -z "$WIFI_SIM" ] || [ ! -f "$WIFI_SIM" ]; then
    echo "WiFi static simulator not found: $WIFI_SIM"
    exit 1
fi

if [ -z "$WPAN_SIM" ] || [ ! -f "$WPAN_SIM" ]; then
    echo "802.15.4 static simulator not found: $WPAN_SIM"
    exit 1
fi

echo "Verified: $WIFI_SIM"
echo "Verified: $WPAN_SIM"
echo ""

echo "[3/4] Preparing output directories..."
mkdir -p "$PROJECT_DIR/results/graphs"
echo "Output directories ready at: $PROJECT_DIR/results/"
echo ""

echo "[4/4] Running quick test (1 WiFi static + 1 802.15.4 static config)..."
echo ""

echo "Testing WiFi 802.11 static network..."
"$WIFI_SIM" \
    --numNodes=20 \
    --numFlows=10 \
    --packetRate=100 \
    --packetSize=512 \
    --simTime=5 \
    --tcpType=TcpWestwood \
    --adaptive=1 \
    --outputDir="$PROJECT_DIR/results"
echo ""

echo "Testing 802.15.4 static network..."
"$WPAN_SIM" \
    --numNodes=20 \
    --numFlows=10 \
    --packetRate=100 \
    --packetSize=100 \
    --simTime=5 \
    --tcpType=TcpWestwood \
    --adaptive=1 \
    --outputDir="$PROJECT_DIR/results"
echo ""

echo "=============================================================="
echo "Quick test complete"
echo ""
echo "Next steps:"
echo "  cd $PROJECT_DIR"
echo "  python3 batch_simulator.py --mode sweep --profile reduced"
echo "  python3 plot_results.py"
echo "=============================================================="
