import glob
import os
import pandas as pd
import matplotlib.pyplot as plt

plt.style.use("seaborn-v0_8-whitegrid")

DATA_DIR = "/tmp/comp_arch"
OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def summarize(df, label):
    c_red = (df["cycle1"] - df["cycle2"]) / df["cycle1"] * 100
    s_red = (
        (df["stalls1"] - df["stalls2"]) / df["stalls1"].replace(0, float("nan")) * 100
    )
    ipc_gain = (df["ipc2"] - df["ipc1"]) / df["ipc1"] * 100
    moved_pct = df["moved"] / df["instr_count"] * 100
    est_err = (df["est_cycle2"] - df["cycle2"]).abs() / df["cycle2"] * 100
    return {
        "Config": label,
        "Runs": len(df),
        "Cycles (Before -> After)": f"{df['cycle1'].mean():.1f} -> {df['cycle2'].mean():.1f}",
        "Cycle Reduction (%)": f"{c_red.mean():.2f}% (max {c_red.max():.2f}%)",
        "Stalls (Before -> After)": f"{df['stalls1'].mean():.1f} -> {df['stalls2'].mean():.1f}",
        "Stall Reduction (%)": f"{s_red.mean():.2f}%",
        "IPC (Before -> After)": f"{df['ipc1'].mean():.3f} -> {df['ipc2'].mean():.3f} (+{ipc_gain.mean():.2f}%)",
        "Instr Moved (%)": f"{moved_pct.mean():.1f}%",
        "Est. Error (%)": f"{est_err.mean():.2f}%",
        "Runtime (ms)": f"{df['reorder_ms'].mean():.2f} ms",
    }


# 1. Summary Table & Chart 1: Forwarding vs Non-Forwarding
rows = []
fwd_dfs = []
for name, path in [
    ("With Forwarding (rv5-h-f)", f"{DATA_DIR}/exp_fwd1.csv"),
    ("No Forwarding (rv5-h-nf)", f"{DATA_DIR}/exp_fwd0.csv"),
]:
    if os.path.exists(path):
        df = pd.read_csv(path)
        rows.append(summarize(df, name))
        fwd_dfs.append((name.split(" (")[0], df))

if rows:
    summary_df = pd.DataFrame(rows)
    print("\n=== PRESENTATION SUMMARY TABLE ===")
    print(summary_df.to_string(index=False))
    summary_df.to_csv(os.path.join(OUT_DIR, "presentation_summary.csv"), index=False)

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5), dpi=300)
    labels = [lbl for lbl, _ in fwd_dfs]
    dfs = [d for _, d in fwd_dfs]

    x = range(len(dfs))
    w = 0.35
    axes[0].bar(
        [i - w / 2 for i in x],
        [d["cycle1"].mean() for d in dfs],
        width=w,
        label="Original",
        color="#64748b",
    )
    axes[0].bar(
        [i + w / 2 for i in x],
        [d["cycle2"].mean() for d in dfs],
        width=w,
        label="Reordered",
        color="#2563eb",
    )
    axes[0].set_title("Mean Execution Cycles (Lower is Better)")
    axes[0].set_xticks(list(x))
    axes[0].set_xticklabels(labels)
    axes[0].set_ylabel("Cycles")
    axes[0].legend()

    axes[1].bar(
        [i - w / 2 for i in x],
        [d["stalls1"].mean() for d in dfs],
        width=w,
        label="Original",
        color="#94a3b8",
    )
    axes[1].bar(
        [i + w / 2 for i in x],
        [d["stalls2"].mean() for d in dfs],
        width=w,
        label="Reordered",
        color="#16a34a",
    )
    axes[1].set_title("Mean Pipeline Stall Cycles (Lower is Better)")
    axes[1].set_xticks(list(x))
    axes[1].set_xticklabels(labels)
    axes[1].set_ylabel("Stall Cycles")
    axes[1].legend()

    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, "chart_fwd_comparison.png"))
    plt.close(fig)

# 2. Chart 2: Memory Intensity Sweep
mem_files = sorted(
    glob.glob(f"{DATA_DIR}/exp_mem_*.csv"),
    key=lambda s: int(s.split("_")[-1].split(".")[0]),
)
if mem_files:
    mem_pts = []
    for f in mem_files:
        d = pd.read_csv(f)
        pct = int(d["mem_pct"].iloc[0])
        gain = ((d["ipc2"] - d["ipc1"]) / d["ipc1"] * 100).mean()
        stall_red = (
            (d["stalls1"] - d["stalls2"]) / d["stalls1"].replace(0, float("nan")) * 100
        ).mean()
        mem_pts.append((pct, gain, stall_red))
    mdf = pd.DataFrame(mem_pts, columns=["mem_pct", "ipc_gain", "stall_red"])

    fig, ax = plt.subplots(figsize=(7, 4.2), dpi=300)
    ax.plot(
        mdf["mem_pct"],
        mdf["ipc_gain"],
        marker="o",
        linewidth=2,
        color="#2563eb",
        label="IPC Gain (%)",
    )
    ax.plot(
        mdf["mem_pct"],
        mdf["stall_red"],
        marker="s",
        linewidth=2,
        color="#16a34a",
        label="Stall Reduction (%)",
    )
    ax.set_title("Scheduler Effectiveness vs. Memory Instruction Density")
    ax.set_xlabel("Memory Instructions (ld/lw/lh/lb/sd/sw/sh/sb %)")
    ax.set_ylabel("Improvement (%)")
    ax.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, "chart_mem_sensitivity.png"))
    plt.close(fig)

# 3. Chart 3: Control-Flow Density Sweep
ctrl_files = sorted(
    glob.glob(f"{DATA_DIR}/exp_ctrl_*.csv"),
    key=lambda s: int(s.split("_")[-1].split(".")[0]),
)
if ctrl_files:
    ctrl_pts = []
    for f in ctrl_files:
        d = pd.read_csv(f)
        cpct = int(d["ctrl_pct"].iloc[0])
        gain = ((d["ipc2"] - d["ipc1"]) / d["ipc1"] * 100).mean()
        stall_red = (
            (d["stalls1"] - d["stalls2"]) / d["stalls1"].replace(0, float("nan")) * 100
        ).mean()
        ctrl_pts.append((cpct, gain, stall_red))
    cdf = pd.DataFrame(ctrl_pts, columns=["ctrl_pct", "ipc_gain", "stall_red"])

    fig, ax = plt.subplots(figsize=(7, 4.2), dpi=300)
    ax.plot(
        cdf["ctrl_pct"],
        cdf["ipc_gain"],
        marker="o",
        linewidth=2,
        color="#7c3aed",
        label="IPC Gain (%)",
    )
    ax.plot(
        cdf["ctrl_pct"],
        cdf["stall_red"],
        marker="s",
        linewidth=2,
        color="#ea580c",
        label="Stall Reduction (%)",
    )
    ax.set_title("Scheduler Effectiveness vs. Control-Flow Density")
    ax.set_xlabel("Control-Flow Instructions (Branch / JAL / Loop %)")
    ax.set_ylabel("Improvement (%)")
    ax.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, "chart_ctrl_sensitivity.png"))
    plt.close(fig)

# 4. Chart 4: Scalability (Program Size vs Runtime & IPC Gain)
len_files = sorted(
    glob.glob(f"{DATA_DIR}/exp_len_*.csv"),
    key=lambda s: int(s.split("_")[-1].split(".")[0]),
)
if len_files:
    len_pts = []
    for f in len_files:
        d = pd.read_csv(f)
        n = int(d["instr_count"].iloc[0])
        gain = ((d["ipc2"] - d["ipc1"]) / d["ipc1"] * 100).mean()
        ms = d["reorder_ms"].mean()
        len_pts.append((n, gain, ms))
    ldf = pd.DataFrame(len_pts, columns=["instr_count", "ipc_gain", "reorder_ms"])

    fig, ax1 = plt.subplots(figsize=(7.5, 4.2), dpi=300)
    ax2 = ax1.twinx()
    l1 = ax1.plot(
        ldf["instr_count"],
        ldf["ipc_gain"],
        marker="o",
        color="#2563eb",
        linewidth=2,
        label="IPC Gain (%)",
    )
    l2 = ax2.plot(
        ldf["instr_count"],
        ldf["reorder_ms"],
        marker="^",
        linestyle="--",
        color="#dc2626",
        linewidth=2,
        label="Scheduler Time (ms)",
    )
    ax1.set_xlabel("Program Length (Instructions)")
    ax1.set_ylabel("IPC Gain (%)", color="#2563eb")
    ax2.set_ylabel("Reorder Tool Time (ms)", color="#dc2626")
    ax1.set_title("IPC Gain and Scheduler Latency vs. Program Size")
    lines = l1 + l2
    ax1.legend(lines, [l.get_label() for l in lines], loc="upper left")
    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, "chart_scalability.png"))
    plt.close(fig)

# 5. Chart 5: Scheduler Cycle Estimator Accuracy (Predicted vs Actual Cycles)
all_exp = [pd.read_csv(f) for f in glob.glob(f"{DATA_DIR}/exp_*.csv")]
if all_exp:
    combined = pd.concat(all_exp, ignore_index=True)
    valid_est = combined[combined["est_cycle2"] > 0]
    if not valid_est.empty:
        fig, ax = plt.subplots(figsize=(6.2, 5.2), dpi=300)
        ax.scatter(
            valid_est["cycle2"],
            valid_est["est_cycle2"],
            alpha=0.25,
            s=14,
            color="#0284c7",
            label="Benchmark Runs",
        )
        lo = min(valid_est["cycle2"].min(), valid_est["est_cycle2"].min())
        hi = max(valid_est["cycle2"].max(), valid_est["est_cycle2"].max())
        ax.plot([lo, hi], [lo, hi], "r--", linewidth=1.5, label="Ideal (y = x)")
        ax.set_title("Scheduler Cost Model Accuracy (Predicted vs. Kites Cycles)")
        ax.set_xlabel("Actual Kites Reordered Cycles")
        ax.set_ylabel("Scheduler Estimated Cycles")
        ax.legend()
        plt.tight_layout()
        plt.savefig(os.path.join(OUT_DIR, "chart_estimator_accuracy.png"))
        plt.close(fig)

print(f"Saved summary CSV and charts to {OUT_DIR}/")
