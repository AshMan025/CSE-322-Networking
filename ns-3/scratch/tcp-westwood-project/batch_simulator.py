#!/usr/bin/env python3
"""
Batch runner for assigned static wireless networks.

Targets:
- Wireless 802.11 (Static)
- Wireless 802.15.4 (Static)

Keeps the same node/flow/packet-rate measurement ranges as the current setup.
"""

import argparse
import csv
import json
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


FULL_NODE_COUNTS = [20, 30, 40, 50, 60]
FULL_FLOW_COUNTS = [10, 15, 20, 25, 30]
FULL_PACKET_RATES = [100, 150, 200, 250, 300]

REDUCED_NODE_COUNTS = [20, 40, 60]
REDUCED_FLOW_COUNTS = [10, 20, 30]
REDUCED_PACKET_RATES = [100, 200, 300]

PREFERRED_BASES = {
    "nodes": 40,
    "flows": 20,
    "rate": 200,
}

TCP_VARIANTS: Sequence[Tuple[str, bool, str]] = [
    ("TcpNewReno", False, "newreno"),
    ("TcpWestwood", False, "westwood_base"),
    ("TcpWestwood", True, "westwood_adaptive"),
]


@dataclass(frozen=True)
class NetworkConfig:
    key: str
    label: str
    binary_stem: str
    file_prefix: str
    csv_name: str
    throughput_key: str


@dataclass(frozen=True)
class SweepConfig:
    node_counts: List[int]
    flow_counts: List[int]
    packet_rates: List[int]
    base_nodes: int
    base_flows: int
    base_rate: int
    profile: str


NETWORKS: Dict[str, NetworkConfig] = {
    "wifi_static": NetworkConfig(
        key="wifi_static",
        label="WiFi 802.11 Static",
        binary_stem="ns3-dev-wireless-802-11-static",
        file_prefix="wifi-802-11-static",
        csv_name="wifi_aggregated.csv",
        throughput_key="Throughput(Mbps)",
    ),
    "wpan_static": NetworkConfig(
        key="wpan_static",
        label="Wireless 802.15.4 Static",
        binary_stem="ns3-dev-wireless-802-15-4-static",
        file_prefix="802-15-4-static",
        csv_name="wpan_aggregated.csv",
        throughput_key="Throughput(kbps)",
    ),
}


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run static wireless TCP Westwood batch simulations")
    parser.add_argument(
        "--mode",
        choices=["sweep", "full"],
        default="sweep",
        help="sweep: independent parameter sweeps; full: Cartesian product",
    )
    parser.add_argument(
        "--networks",
        nargs="+",
        default=["wifi_static", "wpan_static"],
        choices=list(NETWORKS.keys()),
        help="Networks to run",
    )
    parser.add_argument(
        "--profile",
        choices=["full", "reduced"],
        default="full",
        help="full = assigned ranges, reduced = lighter exploratory subset",
    )
    parser.add_argument("--node-values", type=str, help="Comma-separated node counts override")
    parser.add_argument("--flow-values", type=str, help="Comma-separated flow counts override")
    parser.add_argument("--rate-values", type=str, help="Comma-separated packet-rate values override")
    parser.add_argument("--packet-size", type=int, default=1024, help="Application payload size in bytes")
    parser.add_argument("--sim-time", type=float, default=60.0, help="Simulation time in seconds")
    parser.add_argument("--timeout", type=int, default=1800, help="Per-run timeout in seconds")
    parser.add_argument("--sleep", type=float, default=0.2, help="Pause between runs in seconds")
    parser.add_argument("--force", action="store_true", help="Re-run even if output file already exists")
    parser.add_argument("--aggregate-only", action="store_true", help="Skip simulations and only aggregate existing files")
    parser.add_argument("--verbose", action="store_true", help="Show simulator stdout/stderr on failures")
    return parser


def parse_csv_ints(raw: Optional[str]) -> Optional[List[int]]:
    if raw is None:
        return None
    values = [chunk.strip() for chunk in raw.split(",")]
    parsed = sorted({int(value) for value in values if value})
    if not parsed:
        raise ValueError("Expected at least one integer value")
    return parsed


def pick_base(preferred: int, values: Sequence[int]) -> int:
    if preferred in values:
        return preferred
    ordered = list(values)
    return ordered[len(ordered) // 2]


def resolve_value_sets(args: argparse.Namespace) -> SweepConfig:
    if args.profile == "reduced":
        node_counts = list(REDUCED_NODE_COUNTS)
        flow_counts = list(REDUCED_FLOW_COUNTS)
        packet_rates = list(REDUCED_PACKET_RATES)
    else:
        node_counts = list(FULL_NODE_COUNTS)
        flow_counts = list(FULL_FLOW_COUNTS)
        packet_rates = list(FULL_PACKET_RATES)

    node_counts = parse_csv_ints(args.node_values) or node_counts
    flow_counts = parse_csv_ints(args.flow_values) or flow_counts
    packet_rates = parse_csv_ints(args.rate_values) or packet_rates

    return SweepConfig(
        node_counts=node_counts,
        flow_counts=flow_counts,
        packet_rates=packet_rates,
        base_nodes=pick_base(PREFERRED_BASES["nodes"], node_counts),
        base_flows=pick_base(PREFERRED_BASES["flows"], flow_counts),
        base_rate=pick_base(PREFERRED_BASES["rate"], packet_rates),
        profile=args.profile,
    )


def unique_preserve_order(items: Iterable[Tuple[int, int, int]]) -> List[Tuple[int, int, int]]:
    seen = set()
    out: List[Tuple[int, int, int]] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def build_sweep_configs(sweep: SweepConfig) -> List[Tuple[int, int, int]]:
    configs: List[Tuple[int, int, int]] = []

    for n in sweep.node_counts:
        configs.append((n, sweep.base_flows, sweep.base_rate))

    for f in sweep.flow_counts:
        configs.append((sweep.base_nodes, f, sweep.base_rate))

    for r in sweep.packet_rates:
        configs.append((sweep.base_nodes, sweep.base_flows, r))

    return unique_preserve_order(configs)


def build_full_configs(sweep: SweepConfig) -> List[Tuple[int, int, int]]:
    configs: List[Tuple[int, int, int]] = []
    for n in sweep.node_counts:
        for f in sweep.flow_counts:
            for r in sweep.packet_rates:
                configs.append((n, f, r))
    return configs


def infer_paths() -> Tuple[Path, Path, Path]:
    project_dir = Path(__file__).resolve().parent
    ns3_root = project_dir.parent.parent
    build_scratch = ns3_root / "build" / "scratch"
    return project_dir, ns3_root, build_scratch


def output_filename(prefix: str, n: int, f: int, r: int, tcp_type: str, adaptive: bool) -> str:
    mode = "adaptive" if (tcp_type == "TcpWestwood" and adaptive) else "base"
    return f"{prefix}_n{n}_f{f}_r{r}_{tcp_type}_{mode}.txt"


def resolve_binary_path(build_scratch: Path, network: NetworkConfig) -> Optional[Path]:
    candidates = sorted(build_scratch.glob(f"{network.binary_stem}-*"))
    if not candidates:
        return None

    preferred_suffixes = ["-optimized", "-default", "-release", "-debug"]
    for suffix in preferred_suffixes:
        for candidate in candidates:
            if candidate.name.endswith(suffix):
                return candidate

    return candidates[0]


def run_one_simulation(
    binary_path: Path,
    output_dir: Path,
    prefix: str,
    n: int,
    f: int,
    r: int,
    tcp_type: str,
    adaptive: bool,
    packet_size: int,
    sim_time: float,
    timeout: int,
    force: bool,
    verbose: bool,
) -> str:
    expected_name = output_filename(prefix, n, f, r, tcp_type, adaptive)
    expected_file = output_dir / expected_name
    timeout_file = expected_file.with_suffix(expected_file.suffix + ".timeout")

    if expected_file.exists() and not force:
        timeout_file.unlink(missing_ok=True)
        return "cached"

    cmd = [
        str(binary_path),
        f"--numNodes={n}",
        f"--numFlows={f}",
        f"--packetRate={r}",
        f"--packetSize={packet_size}",
        f"--simTime={sim_time}",
        f"--tcpType={tcp_type}",
        f"--adaptive={1 if adaptive else 0}",
        f"--outputDir={output_dir}",
    ]

    started = time.time()
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        timeout_file.write_text(str(timeout), encoding="utf-8")
        elapsed = time.time() - started
        print(f"  TIMEOUT ({elapsed:.1f}s): {expected_name}")
        return "timeout"
    except Exception as exc:  # pragma: no cover
        elapsed = time.time() - started
        print(f"  ERROR ({elapsed:.1f}s): {expected_name} -> {exc}")
        return "error"

    elapsed = time.time() - started
    if result.returncode != 0:
        print(f"  FAIL ({elapsed:.1f}s): {expected_name} (exit={result.returncode})")
        if verbose:
            if result.stdout.strip():
                print("  STDOUT:")
                print(result.stdout.strip())
            if result.stderr.strip():
                print("  STDERR:")
                print(result.stderr.strip())
        return "failed"

    timeout_file.unlink(missing_ok=True)
    print(f"  OK ({elapsed:.1f}s): {expected_name}")
    if verbose and result.stdout.strip():
        print(result.stdout.strip())
    return "success"


def parse_result_file(path: Path) -> Dict[str, str]:
    parsed: Dict[str, str] = {}
    with path.open("r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or ":" not in line:
                continue
            key, value = line.split(":", 1)
            parsed[key.strip()] = value.strip()
    return parsed


def to_int(value: str) -> int:
    return int(float(value))


def sort_rows(rows: List[Dict[str, str]]) -> List[Dict[str, str]]:
    tcp_order = {"TcpNewReno": 0, "TcpWestwood": 1}
    adaptive_order = {"No": 0, "Yes": 1}

    def sort_key(row: Dict[str, str]) -> Tuple[int, int, int, int, int]:
        n = to_int(row.get("NumNodes", "0"))
        f = to_int(row.get("NumFlows", "0"))
        r = to_int(row.get("PacketRate(pps)", "0"))
        tcp = tcp_order.get(row.get("TCPType", ""), 99)
        adaptive = adaptive_order.get(row.get("AdaptiveTau", "No"), 99)
        return (n, f, r, tcp, adaptive)

    return sorted(rows, key=sort_key)


def aggregate_results(output_dir: Path) -> None:
    print("\n" + "=" * 72)
    print("Aggregating Results")
    print("=" * 72)

    common_columns = [
        "Network",
        "NumNodes",
        "NumFlows",
        "PacketRate(pps)",
        "TCPType",
        "AdaptiveTau",
        "E2EDelay(ms)",
        "PDR",
        "DropRatio",
        "EnergyConsumed(J)",
    ]

    for network in NETWORKS.values():
        rows: List[Dict[str, str]] = []
        for file_path in sorted(output_dir.glob(f"{network.file_prefix}_*.txt")):
            parsed = parse_result_file(file_path)
            if not parsed:
                continue
            parsed.setdefault("EnergyConsumed(J)", "")
            parsed["Network"] = network.label
            rows.append(parsed)

        csv_path = output_dir / network.csv_name
        if not rows:
            if csv_path.exists():
                csv_path.unlink()
            print(f"No rows for {network.label} ({network.file_prefix}_*.txt)")
            continue

        sorted_rows = sort_rows(rows)
        columns = common_columns[:6] + [network.throughput_key] + common_columns[6:]

        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(sorted_rows)

        print(f"Wrote {len(sorted_rows)} rows -> {csv_path}")


def write_sweep_manifest(output_dir: Path, sweep: SweepConfig, networks: Sequence[NetworkConfig], mode: str) -> None:
    manifest = {
        "profile": sweep.profile,
        "mode": mode,
        "node_counts": sweep.node_counts,
        "flow_counts": sweep.flow_counts,
        "packet_rates": sweep.packet_rates,
        "base_nodes": sweep.base_nodes,
        "base_flows": sweep.base_flows,
        "base_rate": sweep.base_rate,
        "networks": [network.key for network in networks],
    }
    path = output_dir / "sweep_config.json"
    path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Wrote sweep manifest -> {path}")


def summarize_coverage(
    output_dir: Path,
    networks: Sequence[NetworkConfig],
    configs_by_network: Dict[str, Sequence[Tuple[int, int, int]]],
) -> None:
    print("\n" + "=" * 72)
    print("Coverage Summary")
    print("=" * 72)

    for network in networks:
        configs = configs_by_network[network.key]
        print(f"\n{network.label}")
        for tcp_type, adaptive, tag in TCP_VARIANTS:
            expected = [
                output_filename(network.file_prefix, n, f, r, tcp_type, adaptive)
                for (n, f, r) in configs
            ]
            present = sum(1 for name in expected if (output_dir / name).exists())
            print(f"  {tag:18s} : {present:3d}/{len(expected):3d}")


def run_network(
    network: NetworkConfig,
    configs: Sequence[Tuple[int, int, int]],
    build_scratch: Path,
    output_dir: Path,
    args: argparse.Namespace,
) -> None:
    binary_path = resolve_binary_path(build_scratch, network)
    if binary_path is None:
        print(
            f"ERROR: simulator not found for {network.label}: "
            f"{build_scratch / (network.binary_stem + '-*')}"
        )
        return

    print("\n" + "=" * 72)
    print(f"{network.label} - Running {len(configs)} parameter configurations")
    print("=" * 72)

    total = len(configs) * len(TCP_VARIANTS)
    current = 0
    status_counts = {"success": 0, "cached": 0, "timeout": 0, "failed": 0, "error": 0}

    for tcp_type, adaptive, tag in TCP_VARIANTS:
        for n, f, r in configs:
            current += 1
            print(f"[{current:03d}/{total:03d}] n={n:3d} f={f:3d} r={r:3d} | {tag}")
            status = run_one_simulation(
                binary_path=binary_path,
                output_dir=output_dir,
                prefix=network.file_prefix,
                n=n,
                f=f,
                r=r,
                tcp_type=tcp_type,
                adaptive=adaptive,
                packet_size=args.packet_size,
                sim_time=args.sim_time,
                timeout=args.timeout,
                force=args.force,
                verbose=args.verbose,
            )
            status_counts[status] = status_counts.get(status, 0) + 1
            if args.sleep > 0:
                time.sleep(args.sleep)

    print(f"\n{network.label} status summary:")
    for key in ["success", "cached", "timeout", "failed", "error"]:
        print(f"  {key:8s}: {status_counts.get(key, 0)}")


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()

    project_dir, ns3_root, build_scratch = infer_paths()
    output_dir = project_dir / "results"
    output_dir.mkdir(parents=True, exist_ok=True)

    sweep = resolve_value_sets(args)
    selected_networks = [NETWORKS[key] for key in args.networks]
    configs_by_network = {
        network.key: build_sweep_configs(sweep) if args.mode == "sweep" else build_full_configs(sweep)
        for network in selected_networks
    }

    print("\n" + "=" * 72)
    print("TCP Westwood Static Wireless Batch Suite")
    print("=" * 72)
    print(f"ns-3 root    : {ns3_root}")
    print(f"build scratch: {build_scratch}")
    print(f"output dir   : {output_dir}")
    print(f"profile      : {sweep.profile}")
    print(f"nodes        : {sweep.node_counts} (base={sweep.base_nodes})")
    print(f"flows        : {sweep.flow_counts} (base={sweep.base_flows})")
    print(f"packet rates : {sweep.packet_rates} (base={sweep.base_rate})")
    print(f"mode         : {args.mode}")
    print(f"packet size  : {args.packet_size} bytes")
    print(f"sim-time     : {args.sim_time}s")
    print(f"timeout      : {args.timeout}s")
    print(f"networks     : {', '.join(n.key for n in selected_networks)}")

    if not args.aggregate_only:
        for network in selected_networks:
            run_network(network, configs_by_network[network.key], build_scratch, output_dir, args)

    aggregate_results(output_dir)
    write_sweep_manifest(output_dir, sweep, selected_networks, args.mode)
    summarize_coverage(output_dir, selected_networks, configs_by_network)

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
