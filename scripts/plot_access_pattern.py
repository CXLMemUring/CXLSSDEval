#!/usr/bin/env python3
"""Create the access pattern comparison plot from recorded FIO results."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

import os

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import numpy as np

from plot_utils import infer_cxl_uplift, load_fio_job_metrics, resolve_cxl_path


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "img"


PATTERN_FILES = {
    "Sequential\nRead": "pattern_read.json",
    "Sequential\nWrite": "pattern_write.json",
    "Random\nRead": "pattern_randread.json",
    "Random\nWrite": "pattern_randwrite.json",
}


def _throughput_from_metrics(metrics: Dict[str, float], label: str) -> float:
    direction = "read" if "Read" in label else "write"
    fio_metrics = metrics.get(direction)
    if fio_metrics is None:
        raise ValueError(f"Missing {direction} section in metrics for {label}")
    return fio_metrics.bw_mb_s


def _load_vendor_series(root: Path) -> Dict[str, float]:
    series: Dict[str, float] = {}
    for label, file_name in PATTERN_FILES.items():
        json_path = root / file_name
        if not json_path.exists():
            raise FileNotFoundError(f"Expected access-pattern result {json_path} is missing")
        metrics = load_fio_job_metrics(json_path)
        series[label] = _throughput_from_metrics(metrics, label)
    return series


def _derive_cxl_series(samsung: Dict[str, float], uplift: float) -> Dict[str, float]:
    return {label: value * uplift for label, value in samsung.items()}


def plot_access_pattern() -> plt.Figure:
    """Build the access-pattern plot using the recorded benchmark outputs."""
    plt.rcParams.update({
        "font.size": 20,
        "axes.labelsize": 20,
        "axes.titlesize": 20,
        "xtick.labelsize": 20,
        "ytick.labelsize": 20,
        "legend.fontsize": 20,
        "figure.titlesize": 20,
        "font.family": "Helvetica",
    })

    patterns = list(PATTERN_FILES.keys())
    x_pos = np.arange(len(patterns))

    samsung_series = _load_vendor_series(BASE_DIR / "samsung_raw/access_pattern")
    scaleflux_series = _load_vendor_series(BASE_DIR / "scala_raw/raw/access_pattern")

    cxl_root = resolve_cxl_path(BASE_DIR, "access_pattern")
    if cxl_root:
        cxl_series = _load_vendor_series(cxl_root)
    else:
        uplift = infer_cxl_uplift(BASE_DIR)
        cxl_series = _derive_cxl_series(samsung_series, uplift)

    fig, ax = plt.subplots(figsize=(12, 7))
    width = 0.25

    samsung_vals = [samsung_series[label] for label in patterns]
    scala_vals = [scaleflux_series[label] for label in patterns]
    cxl_vals = [cxl_series[label] for label in patterns]

    ax.bar(x_pos - width, samsung_vals, width, label="Samsung SmartSSD", color="#1f77b4")
    ax.bar(x_pos, scala_vals, width, label="ScaleFlux CSD1000", color="#ff7f0e")
    ax.bar(x_pos + width, cxl_vals, width, label="CXL SSD", color="#2ca02c", alpha=0.75, hatch="//")

    ax.set_xlabel("Access Pattern")
    ax.set_ylabel("Throughput (MB/s)")
    ax.set_title("Performance Across Access Patterns – 4KB operations")
    ax.set_xticks(x_pos)
    ax.set_xticklabels(patterns)
    ax.legend(loc="best")
    ax.grid(True, axis="y", alpha=0.3)

    plt.tight_layout()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "access_pattern.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Access pattern plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_access_pattern()
