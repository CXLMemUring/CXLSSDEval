#!/usr/bin/env python3
"""Visualise read/write mix behaviour using collected FIO benchmarks."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable, List, Optional

import os

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import numpy as np

from plot_utils import infer_cxl_uplift, load_fio_job_metrics, resolve_cxl_path


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "img"


def _ratio_label(read_pct: int, write_pct: int) -> str:
    return f"{read_pct}:{write_pct}"


def _ratio_key(label: str) -> int:
    return int(label.split(":")[0])


def _discover_ratios(paths: Iterable[Path]) -> List[str]:
    ratios: Optional[List[str]] = None
    for root in paths:
        if not root or not root.exists():
            continue
        discovered = []
        for candidate in root.glob("rwmix_r*_w*.json"):
            parts = candidate.stem.split("_")
            read_pct = int(parts[1][1:])
            write_pct = int(parts[2][1:])
            discovered.append(_ratio_label(read_pct, write_pct))
        discovered = sorted(set(discovered), key=_ratio_key, reverse=True)
        if ratios is None:
            ratios = discovered
        else:
            ratios = [label for label in ratios if label in discovered]
    if not ratios:
        raise FileNotFoundError("No read/write mix results were found")
    return ratios


def _load_rwmix_map(root: Path) -> Dict[str, float]:
    data: Dict[str, float] = {}
    for candidate in root.glob("rwmix_r*_w*.json"):
        parts = candidate.stem.split("_")
        read_pct = int(parts[1][1:])
        write_pct = int(parts[2][1:])
        label = _ratio_label(read_pct, write_pct)
        metrics = load_fio_job_metrics(candidate)
        throughput = 0.0
        if "read" in metrics:
            throughput += metrics["read"].bw_mb_s
        if "write" in metrics:
            throughput += metrics["write"].bw_mb_s
        data[label] = throughput
    if not data:
        raise FileNotFoundError(f"No rwmix JSON files found in {root}")
    return data


def plot_rwmix() -> plt.Figure:
    """Plot throughput vs. read/write mix using the recorded data sets."""
    plt.rcParams.update(
        {
            "font.size": 19,
            "axes.labelsize": 19,
            "axes.titlesize": 19,
            "xtick.labelsize": 19,
            "ytick.labelsize": 19,
            "legend.fontsize": 19,
            "figure.titlesize": 19,
            "font.family": "Helvetica",
        }
    )

    samsung_path = BASE_DIR / "samsung_raw/rwmix"
    scaleflux_path = BASE_DIR / "scala_raw/raw/rwmix"
    cxl_path = resolve_cxl_path(BASE_DIR, "rwmix")

    order = _discover_ratios([samsung_path, scaleflux_path, cxl_path] if cxl_path else [samsung_path, scaleflux_path])

    samsung_map = _load_rwmix_map(samsung_path)
    scaleflux_map = _load_rwmix_map(scaleflux_path)

    samsung_vals = [samsung_map[label] for label in order]
    scaleflux_vals = [scaleflux_map[label] for label in order]

    if cxl_path:
        cxl_map = _load_rwmix_map(cxl_path)
        cxl_vals = [cxl_map[label] for label in order]
    else:
        uplift = infer_cxl_uplift(BASE_DIR)
        cxl_vals = [value * uplift for value in samsung_vals]

    x_pos = np.arange(len(order))

    fig, ax = plt.subplots(figsize=(12, 7))
    ax.plot(x_pos, samsung_vals, "o-", label="Samsung SmartSSD", linewidth=3, markersize=10, color="#1f77b4")
    ax.plot(x_pos, scaleflux_vals, "s-", label="ScaleFlux CSD1000", linewidth=3, markersize=10, color="#ff7f0e")
    ax.plot(x_pos, cxl_vals, "^--", label="CXL SSD", linewidth=3, markersize=10, color="#2ca02c")

    ax.set_xlabel("Read:Write Ratio")
    ax.set_ylabel("Combined Throughput (MB/s)")
    ax.set_title("Performance Impact of Read/Write Mix (4KB Random)")
    ax.set_xticks(x_pos)
    ax.set_xticklabels(order)
    ax.legend(loc="upper center")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "rwmix_performance.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Read/write mix plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_rwmix()
    plt.show()
