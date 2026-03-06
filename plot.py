#!/usr/bin/env python3
"""
Plot compression benchmark results.
Usage: ./cluster_bench [files...] > results.csv
       python3 plot.py results.csv
"""

import sys

import matplotlib
import matplotlib.pyplot as plt
import pandas as pd

matplotlib.rcParams.update({"font.size": 12})

ALGO_COLORS = {
    "zstd": "#1f77b4",
    "lz4": "#2ca02c",
    "zlib": "#d62728",
    "brotli": "#9467bd",
    "snappy": "#ff7f0e",
}
LAYOUT_MARKERS = {"SoA_U": "o", "AoS_U": "s", "SoA_P": "x", "AoS_P": "p"}
DICT_STYLE = {False: "-", True: "--"}


def load(path):
    df = pd.read_csv(path)
    df["Dict"] = df["Dict"].str.strip() == "yes"
    df["Label"] = df["Algo"] + "-" + df["Level"].astype(str)
    return df


def plot_scatter(df, x_col, y_col, x_label, y_label, title, outfile):
    fig, ax = plt.subplots(figsize=(10, 7))

    for _, row in df.iterrows():
        color = ALGO_COLORS.get(row["Algo"], "grey")
        marker = LAYOUT_MARKERS.get(row["Layout"], "x")
        edge = "black" if row["Dict"] else color
        fill = "none" if row["Dict"] else color
        if row["Dict"]:  # dict do not seem to help so drop it
            continue

        ax.scatter(
            row[x_col],
            row[y_col],
            c=fill,
            edgecolors=edge,
            marker=marker,
            s=100,
            linewidths=1.5,
            zorder=3,
        )
        ax.annotate(
            row["Label"],
            (row[x_col], row[y_col]),
            textcoords="offset points",
            xytext=(5, 5),
            fontsize=6,
            alpha=0.8,
        )

    # Legend entries
    for algo, color in ALGO_COLORS.items():
        ax.scatter([], [], c=color, label=algo, s=80)
    for layout, marker in LAYOUT_MARKERS.items():
        ax.scatter([], [], c="grey", marker=marker, label=layout, s=80)
    ax.scatter(
        [], [], c="none", edgecolors="black", label="with dict", s=80, linewidths=1.5
    )

    # TopoDict
    topoDictComp = 1.89
    plt.axvline(x=topoDictComp, color="red", linestyle="--", label="TopoDict")

    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_yscale("log")
    ax.set_title(title)
    ax.legend(loc="best", fontsize=10)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(outfile, dpi=150)
    print(f"Saved {outfile}", file=sys.stderr)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} results.csv", file=sys.stderr)
        sys.exit(1)

    df = load(sys.argv[1])

    plot_scatter(
        df,
        "Ratio",
        "DecodeMBs",
        "Compression Ratio (higher=better)",
        "Decode Throughput (MB/s) (higher=better)",
        "Compression Ratio vs Decode Speed",
        "ratio_vs_decode.png",
    )

    plot_scatter(
        df,
        "Ratio",
        "EncodeMBs",
        "Compression Ratio (higher=better)",
        "Encode Throughput (MB/s) (higher=better)",
        "Compression Ratio vs Encode Speed",
        "ratio_vs_encode.png",
    )


if __name__ == "__main__":
    main()
