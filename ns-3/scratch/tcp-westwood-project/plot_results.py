#!/usr/bin/env python3
"""
Generate comparison plots for static wireless TCP Westwood networks.

Networks:
- WiFi 802.11 Static
- Wireless 802.15.4 Static
"""

import csv
import json
import os
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Keep matplotlib/font cache writable even in restricted environments.
PROJECT_DIR = Path(__file__).resolve().parent
MPL_CACHE_DIR = PROJECT_DIR / ".mpl-cache"
MPL_CACHE_DIR.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MPL_CACHE_DIR))
os.environ.setdefault("XDG_CACHE_HOME", str(MPL_CACHE_DIR))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


DEFAULT_SWEEP = {
    "node_counts": [20, 30, 40, 50, 60],
    "flow_counts": [10, 15, 20, 25, 30],
    "packet_rates": [100, 150, 200, 250, 300],
    "base_nodes": 40,
    "base_flows": 20,
    "base_rate": 200,
}

NETWORKS = {
    "wifi_static": {
        "label": "WiFi 802.11 Static",
        "csv_name": "wifi_aggregated.csv",
        "graph_prefix": "wifi",
        "throughput_col": "Throughput(Mbps)",
        "throughput_ylabel": "Throughput (Mbps)",
    },
    "wpan_static": {
        "label": "Wireless 802.15.4 Static",
        "csv_name": "wpan_aggregated.csv",
        "graph_prefix": "wpan",
        "throughput_col": "Throughput(kbps)",
        "throughput_ylabel": "Throughput (kbps)",
    },
}

PROTOCOL_ORDER = ["TcpNewReno", "TcpWestwood_base", "TcpWestwood_adaptive"]
PROTOCOL_LABELS = {
    "TcpNewReno": "TCP NewReno",
    "TcpWestwood_base": "TCP Westwood (Base)",
    "TcpWestwood_adaptive": "TCP Westwood (Adaptive)",
}
PROTOCOL_COLORS = {
    "TcpNewReno": "#e74c3c",
    "TcpWestwood_base": "#3498db",
    "TcpWestwood_adaptive": "#2ecc71",
}
PROTOCOL_MARKERS = {
    "TcpNewReno": "^",
    "TcpWestwood_base": "s",
    "TcpWestwood_adaptive": "o",
}

plt.style.use("seaborn-v0_8-whitegrid")
plt.rcParams["figure.figsize"] = (10, 6)
plt.rcParams["font.size"] = 11
plt.rcParams["lines.linewidth"] = 2.2
plt.rcParams["lines.markersize"] = 7


def load_csv(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_sweep_config(results_dir: Path) -> Dict[str, object]:
    manifest = results_dir / "sweep_config.json"
    if not manifest.exists():
        return dict(DEFAULT_SWEEP)
    return {**DEFAULT_SWEEP, **json.loads(manifest.read_text(encoding="utf-8"))}


def to_int(row: Dict[str, str], key: str) -> Optional[int]:
    try:
        value = row.get(key, "")
        if value in ("", None):
            return None
        return int(float(value))
    except (TypeError, ValueError):
        return None


def to_float(row: Dict[str, str], key: str) -> Optional[float]:
    try:
        value = row.get(key, "")
        if value in ("", None):
            return None
        return float(value)
    except (TypeError, ValueError):
        return None


def protocol_key(row: Dict[str, str]) -> Optional[str]:
    tcp_type = (row.get("TCPType") or "").strip()
    adaptive_str = (row.get("AdaptiveTau") or "").strip().lower()
    adaptive = adaptive_str in {"yes", "true", "1"}

    if tcp_type == "TcpNewReno":
        return "TcpNewReno"
    if tcp_type == "TcpWestwood":
        return "TcpWestwood_adaptive" if adaptive else "TcpWestwood_base"
    return None


def row_matches_variation(row: Dict[str, str], vary_param: str, sweep: Dict[str, object]) -> bool:
    n = to_int(row, "NumNodes")
    f = to_int(row, "NumFlows")
    r = to_int(row, "PacketRate(pps)")
    if n is None or f is None or r is None:
        return False

    base_nodes = int(sweep["base_nodes"])
    base_flows = int(sweep["base_flows"])
    base_rate = int(sweep["base_rate"])

    if vary_param == "NumNodes":
        return f == base_flows and r == base_rate
    if vary_param == "NumFlows":
        return n == base_nodes and r == base_rate
    if vary_param == "PacketRate(pps)":
        return n == base_nodes and f == base_flows
    return False


def expected_x_values(vary_param: str, sweep: Dict[str, object]) -> List[int]:
    mapping = {
        "NumNodes": list(sweep["node_counts"]),
        "NumFlows": list(sweep["flow_counts"]),
        "PacketRate(pps)": list(sweep["packet_rates"]),
    }
    return mapping.get(vary_param, [])


def x_axis_label(vary_param: str) -> str:
    labels = {
        "NumNodes": "Number of Nodes",
        "NumFlows": "Number of Flows",
        "PacketRate(pps)": "Packets per Second",
    }
    return labels.get(vary_param, vary_param)


def filename_token(value: str) -> str:
    token = value.replace("(pps)", "pps")
    token = token.replace("/", "_")
    token = token.replace("(", "")
    token = token.replace(")", "")
    token = token.replace(" ", "_")
    return token


def metric_column(network_key: str, metric: str) -> Optional[str]:
    mapping = {
        "throughput": NETWORKS[network_key]["throughput_col"],
        "delay": "E2EDelay(ms)",
        "pdr": "PDR",
        "drop": "DropRatio",
        "energy": "EnergyConsumed(J)",
    }
    return mapping.get(metric)


def metric_label(network_key: str, metric: str) -> Tuple[str, str]:
    if metric == "throughput":
        return "Network Throughput", NETWORKS[network_key]["throughput_ylabel"]
    if metric == "delay":
        return "End-to-End Delay", "E2E Delay (ms)"
    if metric == "pdr":
        return "Packet Delivery Ratio", "PDR"
    if metric == "drop":
        return "Packet Drop Ratio", "Drop Ratio"
    if metric == "energy":
        return "Energy Consumption", "Energy Consumed (J)"
    return metric, metric


def build_series(
    rows: List[Dict[str, str]],
    vary_param: str,
    metric_col: str,
    sweep: Dict[str, object],
) -> Dict[str, Dict[int, float]]:
    grouped: Dict[str, Dict[int, List[float]]] = defaultdict(lambda: defaultdict(list))

    for row in rows:
        if not row_matches_variation(row, vary_param, sweep):
            continue

        pkey = protocol_key(row)
        if pkey is None:
            continue

        x_val = to_int(row, vary_param)
        y_val = to_float(row, metric_col)
        if x_val is None or y_val is None:
            continue

        grouped[pkey][x_val].append(y_val)

    averaged: Dict[str, Dict[int, float]] = {}
    for pkey, x_map in grouped.items():
        averaged[pkey] = {}
        for x_val, values in x_map.items():
            if values:
                averaged[pkey][x_val] = float(np.mean(values))

    return averaged


def format_coverage_text(series: Dict[str, Dict[int, float]], x_values: List[int]) -> str:
    parts: List[str] = []
    total = len(x_values)
    for key in PROTOCOL_ORDER:
        have = len(series.get(key, {}))
        parts.append(f"{PROTOCOL_LABELS[key]} {have}/{total}")
    return " | ".join(parts)


def plot_metric(
    rows: List[Dict[str, str]],
    vary_param: str,
    metric: str,
    network_key: str,
    network_label: str,
    output_file: Path,
    sweep: Dict[str, object],
) -> bool:
    metric_col = metric_column(network_key, metric)
    if metric_col is None:
        return False

    series = build_series(rows, vary_param, metric_col, sweep)
    x_values = expected_x_values(vary_param, sweep)
    if not x_values:
        return False

    if metric == "energy" and not any(series.get(key) for key in PROTOCOL_ORDER):
        return False

    title, y_label = metric_label(network_key, metric)
    fig, ax = plt.subplots()

    anything_plotted = False
    for key in PROTOCOL_ORDER:
        y_values = [series.get(key, {}).get(x, np.nan) for x in x_values]
        if np.all(np.isnan(y_values)):
            continue
        ax.plot(
            x_values,
            y_values,
            marker=PROTOCOL_MARKERS[key],
            color=PROTOCOL_COLORS[key],
            label=PROTOCOL_LABELS[key],
        )
        anything_plotted = True

    if not anything_plotted:
        plt.close(fig)
        return False

    ax.set_xlabel(x_axis_label(vary_param), fontweight="bold")
    ax.set_ylabel(y_label, fontweight="bold")
    ax.set_title(f"{network_label}: {title}")
    ax.legend(loc="best", fontsize=9)
    ax.grid(True, alpha=0.35)
    ax.text(
        0.01,
        0.02,
        f"Coverage: {format_coverage_text(series, x_values)}",
        transform=ax.transAxes,
        fontsize=8,
        alpha=0.85,
    )

    fig.tight_layout()
    fig.savefig(output_file, dpi=220, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {output_file}")
    return True


def write_coverage_report(
    rows: List[Dict[str, str]],
    network_key: str,
    network_label: str,
    output_file: Path,
    sweep: Dict[str, object],
) -> None:
    vary_params = ["NumNodes", "NumFlows", "PacketRate(pps)"]

    with output_file.open("w", encoding="utf-8") as handle:
        handle.write(f"Coverage report for {network_label}\n")
        handle.write("=" * 72 + "\n")

        for vary_param in vary_params:
            x_values = expected_x_values(vary_param, sweep)
            series = build_series(rows, vary_param, NETWORKS[network_key]["throughput_col"], sweep)
            handle.write(f"\n{vary_param}:\n")
            for key in PROTOCOL_ORDER:
                have = len(series.get(key, {}))
                missing_x = [x for x in x_values if x not in series.get(key, {})]
                handle.write(
                    f"  {PROTOCOL_LABELS[key]}: {have}/{len(x_values)}"
                    f" | missing={missing_x}\n"
                )


def generate_network_plots(results_dir: Path, network_key: str, sweep: Dict[str, object]) -> None:
    spec = NETWORKS[network_key]
    network_label = spec["label"]

    print("\n" + "=" * 72)
    print(f"Generating plots for {network_label}")
    print("=" * 72)

    rows = load_csv(results_dir / spec["csv_name"])
    if not rows:
        print(f"No data found: {spec['csv_name']}")
        return

    graphs_dir = results_dir / "graphs"
    graphs_dir.mkdir(parents=True, exist_ok=True)

    for vary_param in ["NumNodes", "NumFlows", "PacketRate(pps)"]:
        for metric in ["throughput", "delay", "pdr", "drop", "energy"]:
            vary_param_token = filename_token(vary_param)
            out = graphs_dir / f"{spec['graph_prefix']}_{metric}_vs_{vary_param_token}.png"
            plot_metric(rows, vary_param, metric, network_key, network_label, out, sweep)

    coverage_file = graphs_dir / f"{spec['graph_prefix']}_coverage.txt"
    write_coverage_report(rows, network_key, network_label, coverage_file, sweep)
    print(f"  Saved: {coverage_file}")


def generate_throughput_comparison(results_dir: Path, sweep: Dict[str, object]) -> None:
    wifi_rows = load_csv(results_dir / NETWORKS["wifi_static"]["csv_name"])
    wpan_rows = load_csv(results_dir / NETWORKS["wpan_static"]["csv_name"])
    if not wifi_rows or not wpan_rows:
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    for ax, rows, network_key in [
        (ax1, wifi_rows, "wifi_static"),
        (ax2, wpan_rows, "wpan_static"),
    ]:
        spec = NETWORKS[network_key]
        series = build_series(rows, "NumNodes", spec["throughput_col"], sweep)
        x_values = expected_x_values("NumNodes", sweep)
        for key in PROTOCOL_ORDER:
            y_values = [series.get(key, {}).get(x, np.nan) for x in x_values]
            if np.all(np.isnan(y_values)):
                continue
            ax.plot(
                x_values,
                y_values,
                marker=PROTOCOL_MARKERS[key],
                color=PROTOCOL_COLORS[key],
                label=PROTOCOL_LABELS[key],
            )

        ax.set_xlabel("Number of Nodes", fontweight="bold")
        ax.set_ylabel(spec["throughput_ylabel"], fontweight="bold")
        ax.set_title(spec["label"], fontweight="bold")
        ax.grid(True, alpha=0.35)
        ax.legend(fontsize=8)

    out = results_dir / "graphs" / "comparison_throughput_vs_nodes.png"
    fig.tight_layout()
    fig.savefig(out, dpi=220, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {out}")


def main() -> None:
    results_dir = PROJECT_DIR / "results"
    (results_dir / "graphs").mkdir(parents=True, exist_ok=True)
    sweep = load_sweep_config(results_dir)

    print("\n" + "=" * 72)
    print("Plotting TCP Westwood Static Wireless Results")
    print("=" * 72)
    print(f"Results dir: {results_dir}")

    generate_network_plots(results_dir, "wifi_static", sweep)
    generate_network_plots(results_dir, "wpan_static", sweep)
    generate_throughput_comparison(results_dir, sweep)

    print("\nDone. Graphs are under results/graphs/")


if __name__ == "__main__":
    main()
