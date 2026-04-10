#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$ROOT_DIR/scratch/tcp-westwood-project"
RESULTS_DIR="$PROJECT_DIR/results"
SIM_TIMEOUT="${SIM_TIMEOUT:-1800}"
ARCHIVE_OLD_RESULTS="${ARCHIVE_OLD_RESULTS:-0}"
FORCE_RERUN="${FORCE_RERUN:-0}"
RESTORE_LATEST_ARCHIVE="${RESTORE_LATEST_ARCHIVE:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
BUILD_PROFILE="${BUILD_PROFILE:-optimized}"

echo "======================================================================"
echo "Starting Assigned Evaluation (802.11 Static + 802.15.4 Static)"
echo "======================================================================"

mkdir -p "$RESULTS_DIR/graphs"

if [ "$RESTORE_LATEST_ARCHIVE" = "1" ] && ! find "$RESULTS_DIR" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
  LATEST_ARCHIVE="$(ls -dt "$PROJECT_DIR"/results_archive_before_* 2>/dev/null | head -n 1 || true)"
  if [ -n "${LATEST_ARCHIVE:-}" ] && [ -d "$LATEST_ARCHIVE" ]; then
    find "$LATEST_ARCHIVE" -mindepth 1 -maxdepth 1 -exec mv {} "$RESULTS_DIR"/ \;
    mkdir -p "$RESULTS_DIR/graphs"
    echo "Restored archived outputs from: $LATEST_ARCHIVE"
  else
    echo "No archive folder found to restore."
  fi
fi

if [ "$ARCHIVE_OLD_RESULTS" = "1" ] && find "$RESULTS_DIR" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
  ARCHIVE_DIR="$PROJECT_DIR/results_archive_before_run_$(date +%Y%m%d_%H%M%S)"
  mkdir -p "$ARCHIVE_DIR"
  find "$RESULTS_DIR" -mindepth 1 -maxdepth 1 -exec mv {} "$ARCHIVE_DIR"/ \;
  mkdir -p "$RESULTS_DIR/graphs"
  echo "Archived previous outputs to: $ARCHIVE_DIR"
else
  echo "Resume mode: keeping existing outputs; completed runs will be skipped."
fi

if [ "$SKIP_BUILD" = "1" ]; then
  echo "Skipping build step (SKIP_BUILD=1)."
else
  echo "Configuring ns-3 (build profile: $BUILD_PROFILE)..."
  "$ROOT_DIR/ns3" configure --build-profile="$BUILD_PROFILE"

  echo "Building assigned simulators..."
  "$ROOT_DIR/ns3" build wireless-802-11-static
  "$ROOT_DIR/ns3" build wireless-802-15-4-static
fi

echo "Running parameter sweeps..."
SIM_ARGS=(--mode sweep --networks wifi_static wpan_static --timeout "$SIM_TIMEOUT")
if [ "$FORCE_RERUN" = "1" ]; then
  SIM_ARGS+=(--force)
fi
python3 -u "$PROJECT_DIR/batch_simulator.py" "${SIM_ARGS[@]}"

echo "Generating plots..."
python3 "$PROJECT_DIR/plot_results.py"

echo "======================================================================"
echo "Done."
echo "Graphs: $RESULTS_DIR/graphs/"
echo "Coverage reports:"
echo "  - $RESULTS_DIR/graphs/wifi_coverage.txt"
echo "  - $RESULTS_DIR/graphs/wpan_coverage.txt"
echo "======================================================================"
