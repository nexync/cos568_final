#!/usr/bin/env python3
"""
plot_hybrid.py – Generate bar plots comparing DynamicPGM, LIPP, HybridPGMLIPP.

Usage:
    python3 scripts/plot_hybrid.py [--results results/] [--out figures/]
                                   [--datasets fb books osmc]

Outputs (saved to --out directory):
    {dataset}_{workload}_throughput.png
    {dataset}_{workload}_index_size.png

Throughput and index-size values are also written to:
    {out}/throughput_summary.csv
    {out}/index_size_summary.csv
"""

import argparse
import os
import glob
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ── helpers ──────────────────────────────────────────────────────────────────

DATASET_LABELS = {
    "fb_100M_public_uint64":    "Facebook",
    "books_100M_public_uint64": "Books",
    "osmc_100M_public_uint64":  "OSMC",
}

WORKLOAD_LABELS = {
    "0.900000i_0m_mix": "90% Insert",
    "0.100000i_0m_mix": "10% Insert",
}

INDEX_ORDER  = ["DynamicPGM", "LIPP", "HybridPGMLIPP", "HybridPGMLIPPAsync"]
INDEX_COLORS = {
    "DynamicPGM":         "#4C72B0",
    "LIPP":               "#DD8452",
    "HybridPGMLIPP":      "#55A868",
    "HybridPGMLIPPAsync": "#C44E52",
}
INDEX_LABELS = {
    "DynamicPGM":         "DPGM",
    "LIPP":               "LIPP",
    "HybridPGMLIPP":      "Hybrid\n(M2)",
    "HybridPGMLIPPAsync": "Hybrid\n(M3)",
}


def parse_csv(filepath: str) -> pd.DataFrame:
    """Load a result CSV (mixed workload format) and return a tidy DataFrame."""
    try:
        df = pd.read_csv(filepath, header=0)
    except Exception as e:
        print(f"  skip {filepath}: {e}")
        return pd.DataFrame()

    # Normalise column names (strip spaces)
    df.columns = df.columns.str.strip()

    # Identify throughput columns (mixed_throughput_mops*)
    tp_cols = [c for c in df.columns if "mixed_throughput" in c.lower()]
    if not tp_cols:
        return pd.DataFrame()

    df["avg_throughput_mops"] = df[tp_cols].apply(pd.to_numeric, errors="coerce").mean(axis=1)

    # Index size (bytes → MB)
    if "index_size_bytes" in df.columns:
        df["index_size_mb"] = pd.to_numeric(df["index_size_bytes"], errors="coerce") / 1e6
    else:
        df["index_size_mb"] = float("nan")

    return df[["index_name", "avg_throughput_mops", "index_size_mb"]]


def best_variant(df: pd.DataFrame) -> pd.DataFrame:
    """For each index, keep the variant with the highest average throughput."""
    best_idx = df.groupby("index_name")["avg_throughput_mops"].idxmax()
    return df.loc[best_idx].reset_index(drop=True)


def decode_filename(path: str):
    """Return (dataset_key, workload_key) from a result CSV filename."""
    base = os.path.basename(path)
    # dataset
    dataset = None
    for d in DATASET_LABELS:
        if d in base:
            dataset = d
            break
    # workload
    workload = None
    for w in WORKLOAD_LABELS:
        if w in base:
            workload = w
            break
    return dataset, workload


def bar_plot(ax, records: dict, ylabel: str, title: str):
    """Draw a grouped bar chart on *ax*. records: {index_name: value}"""
    names  = [n for n in INDEX_ORDER if n in records]
    values = [records[n] for n in names]
    colors = [INDEX_COLORS.get(n, "#999") for n in names]
    labels = [INDEX_LABELS.get(n, n) for n in names]
    x = np.arange(len(names))
    bars = ax.bar(x, values, color=colors, edgecolor="black", linewidth=0.6, width=0.5)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=9)
    ax.set_ylabel(ylabel, fontsize=10)
    ax.set_title(title, fontsize=10, pad=4)
    ax.yaxis.grid(True, linestyle="--", alpha=0.7)
    ax.set_axisbelow(True)
    for bar, v in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() * 1.02,
                f"{v:.1f}", ha="center", va="bottom", fontsize=7.5)


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Plot hybrid benchmark results")
    parser.add_argument("--results", default="results",  help="Directory with CSV files")
    parser.add_argument("--out",     default="figures",  help="Output directory for plots")
    parser.add_argument("--datasets", nargs="+",
                        default=list(DATASET_LABELS.keys()),
                        help="Which datasets to plot")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)

    # Collect all CSV rows, keyed by (dataset, workload)
    records: dict = {}   # (dataset, workload) -> list of rows

    csv_files = sorted(glob.glob(os.path.join(args.results, "*.csv")))
    if not csv_files:
        print(f"No CSV files found in {args.results}/")
        return

    for fpath in csv_files:
        dataset, workload = decode_filename(fpath)
        if dataset is None or workload is None:
            continue
        if dataset not in args.datasets:
            continue
        df = parse_csv(fpath)
        if df.empty:
            continue
        key = (dataset, workload)
        records.setdefault(key, []).append(df)

    if not records:
        print("No matching data found. Check that the CSV files are in mixed-workload format.")
        return

    # Summary tables
    throughput_rows = []
    index_size_rows = []

    for (dataset, workload), dfs in sorted(records.items()):
        combined = pd.concat(dfs, ignore_index=True)
        best = best_variant(combined)

        tp_map   = dict(zip(best["index_name"], best["avg_throughput_mops"]))
        size_map = dict(zip(best["index_name"], best["index_size_mb"]))

        ds_label = DATASET_LABELS.get(dataset, dataset)
        wl_label = WORKLOAD_LABELS.get(workload, workload)
        stem     = f"{dataset}_{workload}"

        # ── throughput plot ──
        fig, ax = plt.subplots(figsize=(5, 4))
        bar_plot(ax, tp_map,
                 ylabel="Throughput (Mops/s)",
                 title=f"{ds_label} – {wl_label}\nThroughput")
        plt.tight_layout()
        out_tp = os.path.join(args.out, f"{stem}_throughput.png")
        fig.savefig(out_tp, dpi=150)
        plt.close(fig)
        print(f"  saved {out_tp}")

        # ── index size plot ──
        valid_sizes = {k: v for k, v in size_map.items() if not np.isnan(v)}
        if valid_sizes:
            fig, ax = plt.subplots(figsize=(5, 4))
            bar_plot(ax, valid_sizes,
                     ylabel="Index Size (MB)",
                     title=f"{ds_label} – {wl_label}\nIndex Size")
            plt.tight_layout()
            out_sz = os.path.join(args.out, f"{stem}_index_size.png")
            fig.savefig(out_sz, dpi=150)
            plt.close(fig)
            print(f"  saved {out_sz}")

        # Accumulate summary rows
        for idx in INDEX_ORDER:
            throughput_rows.append({
                "dataset": ds_label, "workload": wl_label,
                "index": idx,
                "avg_throughput_mops": tp_map.get(idx, float("nan")),
            })
            index_size_rows.append({
                "dataset": ds_label, "workload": wl_label,
                "index": idx,
                "index_size_mb": size_map.get(idx, float("nan")),
            })

    # Write summary CSVs
    tp_path = os.path.join(args.out, "throughput_summary.csv")
    sz_path = os.path.join(args.out, "index_size_summary.csv")
    pd.DataFrame(throughput_rows).to_csv(tp_path, index=False)
    pd.DataFrame(index_size_rows).to_csv(sz_path, index=False)
    print(f"\nSummary tables written to:\n  {tp_path}\n  {sz_path}")



if __name__ == "__main__":
    main()
