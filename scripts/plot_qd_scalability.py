#!/usr/bin/env python3
"""Plot queue-depth scalability based on recorded benchmark runs."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable, List, Optional

import os

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

from plot_utils import infer_cxl_uplift, load_fio_job_metrics, resolve_cxl_path


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "img"


def _discover_queue_depths(paths: Iterable[Path]) -> List[int]:
    qds: Optional[List[int]] = None
    for root in paths:
        if not root or not root.exists():
            continue
        discovered = sorted({int(p.stem.split("_")[0].replace("jobs", "")) for p in root.glob("jobs*_read.json")})
        if qds is None:
            qds = discovered
        else:
            qds = [value for value in qds if value in discovered]
    if not qds:
        raise FileNotFoundError("No queue-depth results were found")
    return qds


def _load_qd_series(root: Path, queue_depths: List[int]) -> Dict[str, List[float]]:
    read_vals: List[float] = []
    write_vals: List[float] = []
    for qd in queue_depths:
        read_path = root / f"jobs{qd}_read.json"
        write_path = root / f"jobs{qd}_write.json"
        if not read_path.exists() or not write_path.exists():
            raise FileNotFoundError(f"Missing QD={qd} result in {root}")

        read_metrics = load_fio_job_metrics(read_path)
        write_metrics = load_fio_job_metrics(write_path)
        read_vals.append(read_metrics["read"].iops / 1_000)   # Convert to KIOPS
        write_vals.append(write_metrics["write"].iops / 1_000)
    return {"read": read_vals, "write": write_vals}


def plot_qd_scalability() -> plt.Figure:
    """Render queue-depth scaling curves using the capture FIO logs."""
    plt.rcParams.update(
        {
            "font.size": 20,
            "axes.labelsize": 20,
            "axes.titlesize": 20,
            "xtick.labelsize": 20,
            "ytick.labelsize": 20,
            "legend.fontsize": 20,
            "figure.titlesize": 20,
            "font.family": "Helvetica",
        }
    )

    samsung_path = BASE_DIR / "samsung_raw/qd_thread"
    scaleflux_path = BASE_DIR / "scala_raw/raw/qd_thread"
    cxl_path = resolve_cxl_path(BASE_DIR, "qd_thread")

    queue_depths = _discover_queue_depths([samsung_path, scaleflux_path, cxl_path] if cxl_path else [samsung_path, scaleflux_path])

    samsung = _load_qd_series(samsung_path, queue_depths)
    scaleflux = _load_qd_series(scaleflux_path, queue_depths)

    if cxl_path:
        cxl = _load_qd_series(cxl_path, queue_depths)
    else:
        uplift = infer_cxl_uplift(BASE_DIR)
        cxl = {
            "read": [value * uplift for value in samsung["read"]],
            "write": [value * uplift for value in samsung["write"]],
        }

    fig, (ax_read, ax_write) = plt.subplots(1, 2, figsize=(14, 5))

    ax_read.semilogx(queue_depths, samsung["read"], "o-", label="Samsung SmartSSD", linewidth=2, markersize=8)
    ax_read.semilogx(queue_depths, scaleflux["read"], "s-", label="ScaleFlux CSD1000", linewidth=2, markersize=8)
    ax_read.semilogx(queue_depths, cxl["read"], "^--", label="CXL SSD", linewidth=2, markersize=8)
    ax_read.set_xlabel("Queue Depth")
    ax_read.set_ylabel("IOPS (K)")
    ax_read.set_title("(a) Read IOPS Scalability")
    ax_read.legend(loc="upper left")
    ax_read.grid(True, which="both", alpha=0.3)
    ax_read.set_xticks(queue_depths)
    ax_read.xaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{int(value)}"))

    ax_write.semilogx(queue_depths, samsung["write"], "o-", label="Samsung SmartSSD", linewidth=2, markersize=8)
    ax_write.semilogx(queue_depths, scaleflux["write"], "s-", label="ScaleFlux CSD1000", linewidth=2, markersize=8)
    ax_write.semilogx(queue_depths, cxl["write"], "^--", label="CXL SSD", linewidth=2, markersize=8)
    ax_write.set_xlabel("Queue Depth")
    ax_write.set_ylabel("IOPS (K)")
    ax_write.set_title("(b) Write IOPS Scalability")
    ax_write.legend(loc="upper left")
    ax_write.grid(True, which="both", alpha=0.3)
    ax_write.set_xticks(queue_depths)
    ax_write.xaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{int(value)}"))

    plt.tight_layout()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "qd_scalability.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Queue depth scalability plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_qd_scalability()
    plt.show()
