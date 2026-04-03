"""
TCP Westwood Simulation Results Graph Generator
================================================
Generates all comparison plots from the scenario CSV outputs.
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import csv
import os
import glob

RESULTS_DIR = "scratch/tcp-westwood-project/results"
GRAPHS_DIR  = "scratch/tcp-westwood-project/graphs"
os.makedirs(GRAPHS_DIR, exist_ok=True)

# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def read_csv(filename):
    """Return list-of-dicts from a CSV with a header row."""
    rows = []
    path = os.path.join(RESULTS_DIR, filename)
    if not os.path.exists(path):
        print(f"  [SKIP] {filename} not found")
        return rows
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    return rows


def jain_index(values):
    n = len(values)
    if n == 0 or sum(v**2 for v in values) == 0:
        return 0.0
    return sum(values)**2 / (n * sum(v**2 for v in values))


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 1: Throughput vs Wireless Packet Error Rate
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario1():
    print("Plotting Scenario 1 (Throughput vs Error Rate)...")
    rows = read_csv("scenario1_results.csv")
    if not rows:
        return

    data = {"TcpNewReno_Base": {}, "TcpWestwood_Base": {}, "TcpWestwood_Adaptive": {}}
    for r in rows:
        try:
            err  = float(r["ErrorRate"])
            prot = r["Protocol"]
            mode = r["Mode"]
            tput = float(r["Throughput"])
            key  = f"{prot}_{mode}"
            if key in data:
                data[key][err] = tput
        except (ValueError, KeyError):
            continue

    errors = sorted(data["TcpNewReno_Base"].keys())
    if not errors:
        print("  [WARN] No data points for Scenario 1")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(errors, [data["TcpNewReno_Base"].get(e, 0)       for e in errors],
            marker='x', linestyle='--', color='steelblue',  label='TCP NewReno')
    ax.plot(errors, [data["TcpWestwood_Base"].get(e, 0)      for e in errors],
            marker='o', linestyle='-',  color='darkorange',  label='TCP Westwood (Base, fixed τ)')
    ax.plot(errors, [data["TcpWestwood_Adaptive"].get(e, 0)  for e in errors],
            marker='s', linestyle='-',  color='green',       label='TCP Westwood (Adaptive τ)')

    ax.set_xscale('log')
    ax.set_xlabel('Packet Error Rate (PER)', fontsize=12)
    ax.set_ylabel('Throughput (Mbps)', fontsize=12)
    ax.set_title('Scenario 1: Throughput vs Wireless Loss Rate\n'
                 '(error on DATA path only — ACK path clean)', fontsize=11)
    ax.legend()
    ax.grid(True, which='both', alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario1_throughput.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario1_throughput.png")


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 2: Throughput vs RTT
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario2():
    print("Plotting Scenario 2 (Throughput vs RTT)...")
    rows = read_csv("scenario2_results.csv")
    if not rows:
        return

    data = {"TcpNewReno_Base": {}, "TcpNewReno_Adaptive": {}, "TcpWestwood_Base": {}, "TcpWestwood_Adaptive": {}}
    for r in rows:
        try:
            delay = float(r["RTT"].replace("ms", ""))
            prot  = r["Protocol"]
            mode  = r["Mode"]
            tput  = float(r["Throughput"])
            key   = f"{prot}_{mode}"
            if key not in data:
                data[key] = {}
            data[key][delay] = tput
        except (ValueError, KeyError):
            continue

    newreno = data.get("TcpNewReno_Base") if data.get("TcpNewReno_Base") else data.get("TcpNewReno_Adaptive", {})
    
    delays = sorted(newreno.keys() | data.get("TcpWestwood_Base", {}).keys())
    if not delays:
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(delays, [newreno.get(d, 0)       for d in delays],
            marker='x', linestyle='--', color='steelblue',  label='TCP NewReno')
    ax.plot(delays, [data["TcpWestwood_Base"].get(d, 0)      for d in delays],
            marker='o', linestyle='-',  color='darkorange',  label='TCP Westwood (Base)')
    ax.plot(delays, [data["TcpWestwood_Adaptive"].get(d, 0)  for d in delays],
            marker='s', linestyle='-',  color='green',       label='TCP Westwood (Adaptive τ)')

    ax.set_xlabel('One-Way Link Delay (ms) — RTT ≈ 2×delay + wired', fontsize=11)
    ax.set_ylabel('Throughput (Mbps)', fontsize=12)
    ax.set_title('Scenario 2: Throughput vs RTT\n'
                 '(Adaptive τ should diverge at high RTT)', fontsize=11)
    ax.legend()
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario2_throughput.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario2_throughput.png")


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 3: Throughput vs Burst Error Rate (line plot, not bar)
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario3():
    print("Plotting Scenario 3 (Burst Error Performance)...")
    rows = read_csv("scenario3_results.csv")
    if not rows:
        return

    data = {"TcpNewReno_Base": {}, "TcpNewReno_Adaptive": {}, "TcpWestwood_Base": {}, "TcpWestwood_Adaptive": {}}
    for r in rows:
        try:
            burst = float(r["BurstRate"])
            prot  = r["Protocol"]
            mode  = r["Mode"]
            tput  = float(r["Throughput"])
            key   = f"{prot}_{mode}"
            if key not in data:
                data[key] = {}
            data[key][burst] = tput
        except (ValueError, KeyError):
            continue

    newreno = data.get("TcpNewReno_Base") if data.get("TcpNewReno_Base") else data.get("TcpNewReno_Adaptive", {})

    rates = sorted(newreno.keys() | data.get("TcpWestwood_Base", {}).keys())
    if not rates:
        print("  [WARN] No data for Scenario 3")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(rates, [newreno.get(r, 0)       for r in rates],
            marker='x', linestyle='--', color='steelblue',  label='TCP NewReno')
    ax.plot(rates, [data["TcpWestwood_Base"].get(r, 0)      for r in rates],
            marker='o', linestyle='-',  color='darkorange',  label='TCP Westwood (Base)')
    ax.plot(rates, [data["TcpWestwood_Adaptive"].get(r, 0)  for r in rates],
            marker='s', linestyle='-',  color='green',       label='TCP Westwood (Adaptive τ)')

    # Annotate effective loss (burst prob × avg burst size 2.5)
    ax2 = ax.twiny()
    ax2.set_xlim(ax.get_xlim())
    ax2.set_xticks(rates)
    ax2.set_xticklabels([f"{r*2.5*100:.0f}%" for r in rates], fontsize=8)
    ax2.set_xlabel("Effective packet loss ≈ BurstRate × 2.5", fontsize=9)

    ax.set_xlabel('Burst Error Rate (probability of burst event)', fontsize=11)
    ax.set_ylabel('Throughput (Mbps)', fontsize=12)
    ax.set_title('Scenario 3: Performance under Burst Errors\n'
                 '(BurstSize uniform [1,4])', fontsize=11)
    ax.legend()
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario3_burst.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario3_burst.png")


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 4: Fairness — grouped bar chart + Jain's Index, per protocol
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario4():
    print("Plotting Scenario 4 (Fairness)...")
    rows = read_csv("scenario4_results.csv")
    if not rows:
        return

    groups = {}   # protocol+mode → list of throughputs
    for r in rows:
        try:
            prot = r["Protocol"]
            mode = r["Mode"]
            tput = float(r["Throughput"])
            key  = f"{prot}\n({mode})"
            groups.setdefault(key, []).append(tput)
        except (ValueError, KeyError):
            continue

    if not groups:
        return

    labels = list(groups.keys())
    jains  = [jain_index(groups[k]) for k in labels]
    means  = [sum(groups[k])/len(groups[k]) for k in labels]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    # Left: per-flow throughput bar chart for each group side by side
    colors = ['steelblue', 'darkorange', 'green']
    x_offset = 0
    bar_width = 0.6
    xticks, xlabels = [], []
    for idx, key in enumerate(labels):
        vals = groups[key]
        xs   = [x_offset + i for i in range(len(vals))]
        ax1.bar(xs, vals, width=bar_width, color=colors[idx % len(colors)],
                alpha=0.7, label=key.replace("\n", " "))
        xticks.append(x_offset + (len(vals) - 1) / 2)
        xlabels.append(key)
        x_offset += len(vals) + 1

    ax1.set_xticks(xticks)
    ax1.set_xticklabels(xlabels, fontsize=9)
    ax1.set_ylabel("Throughput (Mbps)", fontsize=11)
    ax1.set_title("Per-Flow Throughput by Protocol", fontsize=11)
    ax1.legend(fontsize=8)
    ax1.grid(axis='y', alpha=0.4)

    # Right: Jain's Fairness Index comparison
    bars = ax2.bar(labels, jains, color=colors[:len(labels)], alpha=0.8)
    ax2.axhline(1.0, linestyle='--', color='black', alpha=0.5, label='Perfect fairness')
    ax2.set_ylim(0, 1.1)
    ax2.set_ylabel("Jain's Fairness Index", fontsize=11)
    ax2.set_title("Fairness Comparison\n(1.0 = perfect)", fontsize=11)
    ax2.legend(fontsize=9)
    for bar, val in zip(bars, jains):
        ax2.text(bar.get_x() + bar.get_width()/2, val + 0.02,
                 f"{val:.3f}", ha='center', fontsize=10)
    ax2.grid(axis='y', alpha=0.4)

    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario4_fairness.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario4_fairness.png")


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 5: Tau Adaptation Over Time
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario5():
    print("Plotting Scenario 5 (Tau Dynamics)...")
    path = os.path.join(RESULTS_DIR, "scenario5_output.txt")
    if not os.path.exists(path):
        print("  [SKIP] scenario5_output.txt not found")
        return

    times, taus = [], []
    with open(path) as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) == 2:
                try:
                    times.append(float(parts[0]))
                    taus.append(float(parts[1]))
                except ValueError:
                    pass

    if not times:
        return

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(times, taus, color='steelblue', linewidth=1.5, label='τ (ms)')
    ax.axvline(60,  color='red',    linestyle='--', alpha=0.7, label='RTT→100ms (t=60s)')
    ax.axvline(120, color='purple', linestyle='--', alpha=0.7, label='RTT→200ms (t=120s)')
    ax.axhline(50,   color='grey', linestyle=':', alpha=0.5, label='τ_min=50ms')
    ax.axhline(2000, color='grey', linestyle=':', alpha=0.5, label='τ_max=2000ms')
    ax.set_xlabel('Simulation Time (s)', fontsize=12)
    ax.set_ylabel('Tau, τ (ms)', fontsize=12)
    ax.set_title('Scenario 5: Adaptive τ Responds to RTT Changes\n'
                 '(τ↑ when RTT increases → smoother BWE)', fontsize=11)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario5_tau.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario5_tau.png")


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 6: Congestion Window (cwnd) Over Time
# ──────────────────────────────────────────────────────────────────────────────


# ──────────────────────────────────────────────────────────────────────────────
# Scenario 7: Two-Hop Wireless Performance
# ──────────────────────────────────────────────────────────────────────────────

def plot_scenario7():
    print("Plotting Scenario 7 (Two-Hop Wireless)...")
    rows = read_csv("scenario7_results.csv")
    if not rows:
        return

    data = {"TcpNewReno_Base": {}, "TcpWestwood_Base": {}, "TcpWestwood_Adaptive": {}}
    for r in rows:
        try:
            err  = float(r["ErrorRate"])
            prot = r["Protocol"]
            mode = r["Mode"]
            tput = float(r["Throughput"])
            key  = f"{prot}_{mode}"
            if key in data:
                data[key][err] = tput
        except (ValueError, KeyError):
            continue

    errors = sorted(data["TcpNewReno_Base"].keys())
    if not errors:
        print("  [WARN] No data points for Scenario 7")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(errors, [data["TcpNewReno_Base"].get(e, 0)       for e in errors],
            marker='x', linestyle='--', color='steelblue',  label='TCP NewReno')
    ax.plot(errors, [data["TcpWestwood_Base"].get(e, 0)      for e in errors],
            marker='o', linestyle='-',  color='darkorange',  label='TCP Westwood (Base)')
    ax.plot(errors, [data["TcpWestwood_Adaptive"].get(e, 0)  for e in errors],
            marker='s', linestyle='-',  color='green',       label='TCP Westwood (Adaptive τ)')

    ax.set_xscale('log')
    ax.set_xlabel('Packet Error Rate per hop (PER)', fontsize=12)
    ax.set_ylabel('Throughput (Mbps)', fontsize=12)
    ax.set_title('Scenario 7: Two-Hop Wireless Performance\n'
                 '(n1-wireless-n2-wireless-n3)', fontsize=11)
    ax.legend()
    ax.grid(True, which='both', alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario7_throughput.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario7_throughput.png")

def plot_scenario6():
    print("Plotting Scenario 6 (cwnd Dynamics)...")

    series = {
        "TCP NewReno":             "scenario6_cwnd_TcpNewReno_Base.csv",
        "Westwood (Base, fixed τ)": "scenario6_cwnd_TcpWestwood_Base.csv",
        "Westwood (Adaptive τ)":   "scenario6_cwnd_TcpWestwood_Adaptive.csv",
    }
    colors = ['steelblue', 'darkorange', 'green']
    styles = ['--', '-', '-']

    fig, ax = plt.subplots(figsize=(10, 5))
    any_data = False

    for (label, fname), color, style in zip(series.items(), colors, styles):
        path = os.path.join(RESULTS_DIR, fname)
        if not os.path.exists(path):
            print(f"  [SKIP] {fname} not found")
            continue
        times, cwnds = [], []
        with open(path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    times.append(float(row["Time"]))
                    cwnds.append(float(row["CwndBytes"]) / 1024.0)  # → KB
                except (ValueError, KeyError):
                    pass
        if times:
            ax.plot(times, cwnds, linewidth=1.2, color=color,
                    linestyle=style, label=label, alpha=0.9)
            any_data = True

    if not any_data:
        print("  [WARN] No cwnd data found for Scenario 6")
        plt.close(fig)
        return

    ax.set_xlabel('Simulation Time (s)', fontsize=12)
    ax.set_ylabel('Congestion Window (KB)', fontsize=12)
    ax.set_title('Scenario 6: Congestion Window Dynamics\n'
                 '(1% wireless loss — Westwood recovers faster after each loss)', fontsize=11)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(GRAPHS_DIR, "scenario6_cwnd.png"), dpi=120)
    plt.close(fig)
    print("  Saved scenario6_cwnd.png")


# ──────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    # plot_scenario1()
    # plot_scenario2()
    # plot_scenario3()
    # plot_scenario4()
    # plot_scenario5()
    plot_scenario6()
    plot_scenario7()
    print("Done.")
