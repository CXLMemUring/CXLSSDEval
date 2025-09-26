#!/usr/bin/env python3
"""Generate byte-addressable comparison plots sourced from recorded CSV summaries."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Dict, Iterable, List, Optional

import os

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import numpy as np

BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path("/home/victoryang00/CXLSSDEval/paper/img")


def _block_key(label: str) -> int:
    label = label.lower()
    if label.endswith("k"):
        return int(float(label[:-1]) * 1024)
    return int(label)


def _format_label(label: str) -> str:
    label = label.upper()
    if label.endswith("K"):
        return f"{label}B"
    return f"{label}B"


def _load_summary(csv_path: Path) -> Dict[str, Dict[str, float]]:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    if not rows:
        raise ValueError(f"CSV file {csv_path} is empty")
    summary: Dict[str, Dict[str, float]] = {}
    for row in rows:
        block = row["block_size"].strip()
        summary[block.lower()] = {
            "bw_mb": float(row["write_bw_kbps"]) / 1024.0,
            "lat_us": float(row["total_lat_avg_us"]),
        }
    return summary


def _common_blocks(datasets: Iterable[Dict[str, Dict[str, float]]]) -> List[str]:
    common: Optional[set[str]] = None
    for data in datasets:
        keys = set(data.keys())
        common = keys if common is None else common & keys
    if not common:
        raise ValueError("No overlapping block sizes across datasets")
    return sorted(common, key=_block_key)


def plot_byte_addressable() -> plt.Figure:
    """Create the byte-addressable performance comparison using recorded summaries."""
    plt.rcParams.update(
        {
            "font.size": 16,
            "axes.labelsize": 16,
            "axes.titlesize": 16,
            "xtick.labelsize": 14,
            "ytick.labelsize": 14,
            "legend.fontsize": 14,
            "figure.titlesize": 16,
        }
    )

    samsung_csv = BASE_DIR / "samsung_byte_addressable_result/samsung_byte_addressable_summary.csv"
    scaleflux_csv = BASE_DIR / "scala_byte_addresable_result/scala_byte_addressable_summary.csv"
    cxl_csv = BASE_DIR / "cxl_byte_addressable_result/cxl_byte_addressable_summary.csv"

    samsung = _load_summary(samsung_csv)
    scaleflux = _load_summary(scaleflux_csv)
    cxl = _load_summary(cxl_csv)

    blocks = _common_blocks([samsung, scaleflux, cxl])[:10]
    labels = [_format_label(label) for label in blocks]
    x_pos = np.arange(len(blocks))
    width = 0.25

    samsung_bw = [samsung[block]["bw_mb"] for block in blocks]
    scaleflux_bw = [scaleflux[block]["bw_mb"] for block in blocks]
    cxl_bw = [cxl[block]["bw_mb"] for block in blocks]

    samsung_lat = [samsung[block]["lat_us"] for block in blocks]
    scaleflux_lat = [scaleflux[block]["lat_us"] for block in blocks]
    cxl_lat = [cxl[block]["lat_us"] for block in blocks]

    fig, (ax_bw, ax_lat) = plt.subplots(1, 2, figsize=(14, 6))

    ax_bw.bar(x_pos - width, samsung_bw, width, label="Samsung SmartSSD", color="#1f77b4")
    ax_bw.bar(x_pos, scaleflux_bw, width, label="ScaleFlux CSD1000", color="#ff7f0e")
    ax_bw.bar(x_pos + width, cxl_bw, width, label="CXL SSD", color="#2ca02c", alpha=0.7, hatch="//")
    ax_bw.set_xlabel("Block Size")
    ax_bw.set_ylabel("Write Bandwidth (MB/s)")
    ax_bw.set_title("(a) Write Bandwidth")
    ax_bw.set_xticks(x_pos)
    ax_bw.set_xticklabels(labels, rotation=45, ha="right")
    ax_bw.legend(loc="upper left")
    ax_bw.set_yscale("log")
    ax_bw.grid(True, axis="y", alpha=0.3)

    ax_lat.bar(x_pos - width, samsung_lat, width, label="Samsung SmartSSD", color="#1f77b4")
    ax_lat.bar(x_pos, scaleflux_lat, width, label="ScaleFlux CSD1000", color="#ff7f0e")
    ax_lat.bar(x_pos + width, cxl_lat, width, label="CXL SSD", color="#2ca02c", alpha=0.7, hatch="//")
    ax_lat.set_xlabel("Block Size")
    ax_lat.set_ylabel("Average Latency (µs)")
    ax_lat.set_title("(b) Latency")
    ax_lat.set_xticks(x_pos)
    ax_lat.set_xticklabels(labels, rotation=45, ha="right")
    ax_lat.legend(loc="upper right")
    ax_lat.grid(True, axis="y", alpha=0.3)

    plt.tight_layout()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "byte_addressable.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Byte-addressable plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_byte_addressable()
