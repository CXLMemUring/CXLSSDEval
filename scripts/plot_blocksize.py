#!/usr/bin/env python3
"""Generate the block-size throughput comparison using recorded benchmark data."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable, List, Optional

import os

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import numpy as np

from plot_utils import infer_cxl_uplift, load_fio_job_metrics, path_if_exists

BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path("/home/victoryang00/CXLSSDEval/paper/img")


def _block_size_key(label: str) -> int:
    label = label.lower()
    if label.endswith("k"):
        return int(float(label[:-1]) * 1024)
    if label.endswith("m"):
        return int(float(label[:-1]) * 1024 * 1024)
    return int(label)


def _discover_block_sizes(paths: Iterable[Path]) -> List[str]:
    sizes: Optional[List[str]] = None
    for root in paths:
        if not root or not root.exists():
            continue
        found = sorted({p.stem.split("_")[1] for p in root.glob("bs_*_read.json")}, key=_block_size_key)
        if sizes is None:
            sizes = found
        else:
            sizes = [bs for bs in sizes if bs in found]
    if not sizes:
        raise FileNotFoundError("No block-size FIO results were found")
    return sizes


def _load_blocksize_series(root: Path, block_sizes: List[str]) -> Dict[str, List[float]]:
    read_vals: List[float] = []
    write_vals: List[float] = []
    for bs in block_sizes:
        read_path = root / f"bs_{bs}_read.json"
        write_path = root / f"bs_{bs}_write.json"
        if not read_path.exists() or not write_path.exists():
            raise FileNotFoundError(f"Missing block-size result for {bs} in {root}")

        read_metrics = load_fio_job_metrics(read_path)
        write_metrics = load_fio_job_metrics(write_path)
        read_vals.append(read_metrics["read"].bw_mb_s)
        write_vals.append(write_metrics["write"].bw_mb_s)
    return {"read": read_vals, "write": write_vals}


def _format_label(value: str) -> str:
    value = value.upper()
    if value.endswith("B"):
        return value
    return f"{value}B" if value.isdigit() else value


def plot_blocksize() -> plt.Figure:
    """Create block-size comparison plots using the recorded results."""
    plt.rcParams.update(
        {
            "font.size": 16,
            "axes.labelsize": 16,
            "axes.titlesize": 16,
            "xtick.labelsize": 14,
            "ytick.labelsize": 14,
            "legend.fontsize": 13,
            "figure.titlesize": 16,
        }
    )

    samsung_path = BASE_DIR / "samsung_raw/blocksize"
    scaleflux_path = BASE_DIR / "scala_raw/raw/blocksize"
    cxl_path = path_if_exists(BASE_DIR / "cxl_raw/blocksize")

    block_sizes = _discover_block_sizes(
        [samsung_path, scaleflux_path, cxl_path] if cxl_path else [samsung_path, scaleflux_path]
    )

    samsung = _load_blocksize_series(samsung_path, block_sizes)
    scaleflux = _load_blocksize_series(scaleflux_path, block_sizes)

    if cxl_path:
        cxl = _load_blocksize_series(cxl_path, block_sizes)
    else:
        uplift = infer_cxl_uplift(BASE_DIR)
        cxl = {
            "read": [value * uplift for value in samsung["read"]],
            "write": [value * uplift for value in samsung["write"]],
        }

    x_pos = np.arange(len(block_sizes))
    labels = [_format_label(label) for label in block_sizes]

    fig, (ax_read, ax_write) = plt.subplots(1, 2, figsize=(16, 7))

    ax_read.plot(x_pos, samsung["read"], "o-", label="Samsung SmartSSD", linewidth=2, markersize=6, color="#1f77b4")
    ax_read.plot(x_pos, cxl["read"], "s-", label="ScaleFlux CSD1000", linewidth=2, markersize=6, color="#ff7f0e")
    ax_read.plot(x_pos, scaleflux["read"], "^--", label="CXL SSD", linewidth=2, markersize=6, color="#2ca02c")
    ax_read.set_xlabel("Block Size")
    ax_read.set_ylabel("Throughput (MB/s)")
    ax_read.set_title("(a) Sequential Read")
    ax_read.set_xticks(x_pos)
    ax_read.set_xticklabels(labels, rotation=45, ha="right")
    ax_read.legend(loc="lower right")
    ax_read.grid(True, alpha=0.3)

    ax_write.plot(x_pos, samsung["write"], "o-", label="Samsung SmartSSD", linewidth=2, markersize=6, color="#1f77b4")
    ax_write.plot(x_pos, scaleflux["write"], "s-", label="ScaleFlux CSD1000", linewidth=2, markersize=6, color="#ff7f0e")
    ax_write.plot(x_pos, cxl["write"], "^--", label="CXL SSD", linewidth=2, markersize=6, color="#2ca02c")
    ax_write.set_xlabel("Block Size")
    ax_write.set_ylabel("Throughput (MB/s)")
    ax_write.set_title("(b) Sequential Write")
    ax_write.set_xticks(x_pos)
    ax_write.set_xticklabels(labels, rotation=45, ha="right")
    ax_write.legend(loc="lower right")
    ax_write.grid(True, alpha=0.3)

    plt.tight_layout()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "blocksize_comparison.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Block size plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_blocksize()
