#!/usr/bin/env python3
"""Plot performance across access distributions with configurable data roots."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, Tuple

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt

try:
    import yaml
except ImportError:  # pragma: no cover - optional dependency
    yaml = None

from plot_utils import load_fio_job_metrics, resolve_cxl_path


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "img"
CONFIG_PATH = BASE_DIR / "config_path.yaml"

LABELS = ["uniform", "zipf", "normal", "pareto"]
PRETTY = {
    "uniform": "Uniform",
    "zipf": "Zipfian",
    "normal": "Normal",
    "pareto": "Pareto",
}
COLORS = {
    "samsung": "#1f77b4",
    "scaleflux": "#ff7f0e",
    "cxl": "#2ca02c",
}


def _load_config() -> Dict[str, Dict[str, str]]:
    if not CONFIG_PATH.exists() or yaml is None:
        return {}
    try:
        return yaml.safe_load(CONFIG_PATH.read_text(encoding="utf-8")) or {}
    except Exception:
        return {}


def _default_roots() -> Dict[str, Path]:
    cxl_default = resolve_cxl_path(BASE_DIR, "distribution")
    return {
        "samsung": BASE_DIR / "samsung_raw/distribution",
        "scaleflux": BASE_DIR / "scala_raw/raw/distribution",
        "cxl": cxl_default if cxl_default else BASE_DIR / "cxl_raw/raw/distribution",
    }


def _resolve_roots() -> Dict[str, Path]:
    defaults = _default_roots()
    overrides = _load_config().get("access_distribution", {})
    roots: Dict[str, Path] = {}
    for key, default in defaults.items():
        candidate = overrides.get(key)
        roots[key] = Path(candidate) if candidate else default
    return roots


def _load_throughput(root: Path, dist: str, direction: str) -> float:
    metrics = load_fio_job_metrics(root / f"dist_{dist}_{direction}.json")
    section = metrics.get(direction)
    if not section:
        raise ValueError(f"Missing {direction} metrics in {root}")
    return section.bw_mb_s


def _build_series(root_map: Dict[str, Path], dist: str) -> Tuple[float, float, float]:
    samsung = _load_throughput(root_map["samsung"], dist, "read")
    scaleflux = _load_throughput(root_map["scaleflux"], dist, "read")
    cxl = _load_throughput(root_map["cxl"], dist, "read")
    samsung_w = _load_throughput(root_map["samsung"], dist, "write")
    scaleflux_w = _load_throughput(root_map["scaleflux"], dist, "write")
    cxl_w = _load_throughput(root_map["cxl"], dist, "write")
    return (samsung, scaleflux, cxl), (samsung_w, scaleflux_w, cxl_w)


def plot_access_distribution() -> plt.Figure:
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

    roots = _resolve_roots()
    x_pos = range(len(LABELS))
    width = 0.25

    read_rows = []
    write_rows = []
    for dist in LABELS:
        read_vals, write_vals = _build_series(roots, dist)
        read_rows.append(read_vals)
        write_rows.append(write_vals)

    fig, (ax_r, ax_w) = plt.subplots(1, 2, figsize=(16, 7))

    # Read throughput
    ax_r.bar([x - width for x in x_pos], [vals[0] for vals in read_rows], width, label="Samsung SmartSSD", color=COLORS["samsung"])
    ax_r.bar(x_pos, [vals[1] for vals in read_rows], width, label="ScaleFlux CSD1000", color=COLORS["scaleflux"])
    ax_r.bar([x + width for x in x_pos], [vals[2] for vals in read_rows], width, label="CXL SSD", color=COLORS["cxl"], alpha=0.75, hatch="//")
    ax_r.set_xlabel("Access Distribution")
    ax_r.set_ylabel("Read Throughput (MB/s)")
    ax_r.set_title("(a) Read Throughput (4KB)")
    ax_r.set_xticks(list(x_pos))
    ax_r.set_xticklabels([PRETTY[d] for d in LABELS])
    ax_r.legend(loc="best")
    ax_r.grid(True, axis="y", alpha=0.3)

    # Write throughput
    ax_w.bar([x - width for x in x_pos], [vals[0] for vals in write_rows], width, label="Samsung SmartSSD", color=COLORS["samsung"])
    ax_w.bar(x_pos, [vals[1] for vals in write_rows], width, label="ScaleFlux CSD1000", color=COLORS["scaleflux"])
    ax_w.bar([x + width for x in x_pos], [vals[2] for vals in write_rows], width, label="CXL SSD", color=COLORS["cxl"], alpha=0.75, hatch="//")
    ax_w.set_xlabel("Access Distribution")
    ax_w.set_ylabel("Write Throughput (MB/s)")
    ax_w.set_title("(b) Write Throughput (4KB)")
    ax_w.set_xticks(list(x_pos))
    ax_w.set_xticklabels([PRETTY[d] for d in LABELS])
    ax_w.legend(loc="best")
    ax_w.grid(True, axis="y", alpha=0.3)
    ymax = max([vals[2] for vals in write_rows] + [vals[2] for vals in read_rows])
    ax_r.set_ylim(0, ymax * 1.1)
    ax_w.set_ylim(0, ymax * 1.1)

    plt.tight_layout()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "access_distribution.pdf"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Access-distribution plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_access_distribution()
