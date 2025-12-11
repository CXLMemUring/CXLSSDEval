#!/usr/bin/env python3
"""Plot endurance behaviour (throughput vs. time) with configurable data roots."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, Tuple

os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from plot_utils import resolve_cxl_path

try:
    import yaml
except ImportError:  # pragma: no cover - optional dependency
    yaml = None

BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "img"
CONFIG_PATH = BASE_DIR / "config_path.yaml"

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
    cxl_default = resolve_cxl_path(BASE_DIR, "endurance")
    return {
        "samsung": BASE_DIR / "samsung_raw/endurance",
        "scaleflux": BASE_DIR / "scala_raw/raw/endurance",
        "cxl": cxl_default if cxl_default else BASE_DIR / "cxl_raw/raw/endurance",
    }


def _resolve_roots() -> Dict[str, Path]:
    defaults = _default_roots()
    overrides = _load_config().get("endurance", {})
    roots: Dict[str, Path] = {}
    for key, default in defaults.items():
        candidate = overrides.get(key)
        roots[key] = Path(candidate) if candidate else default
    return roots


def _load_bw_log(path: Path) -> Tuple[np.ndarray, np.ndarray]:
    times_ms = []
    bw_kbps = []
    for line in path.read_text().strip().splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 2:
            continue
        try:
            times_ms.append(float(parts[0]))
            bw_kbps.append(float(parts[1]))
        except ValueError:
            continue
    if not times_ms:
        raise ValueError(f"No data parsed from {path}")
    return np.array(times_ms), np.array(bw_kbps)


def _load_series(root: Path) -> Tuple[np.ndarray, np.ndarray]:
    bw_path = root / "endurance_bw_bw.1.log"
    times_ms, bw_kbps = _load_bw_log(bw_path)
    times_min = times_ms / 60_000.0
    bw_mb_s = bw_kbps / 1024.0
    # Smooth slightly for readability
    series = pd.Series(bw_mb_s)
    smooth_bw = series.rolling(window=10, min_periods=1).mean().to_numpy()
    return times_min, smooth_bw


def plot_endurance() -> plt.Figure:
    plt.rcParams.update(
        {
            "font.size": 18,
            "axes.labelsize": 18,
            "axes.titlesize": 18,
            "xtick.labelsize": 18,
            "ytick.labelsize": 18,
            "legend.fontsize": 18,
            "figure.titlesize": 18,
            "font.family": "Helvetica",
        }
    )

    roots = _resolve_roots()
    fig, ax = plt.subplots(figsize=(14, 7))

    for label, key in (("Samsung SmartSSD", "samsung"), ("ScaleFlux CSD1000", "scaleflux"), ("CXL SSD", "cxl")):
        times, bw = _load_series(roots[key])
        ax.plot(times, bw, label=label, linewidth=2, color=COLORS[key])

    ax.set_xlabel("Time (minutes)")
    ax.set_ylabel("Throughput (MB/s)")
    ax.set_title("Endurance Under 70/30 Read/Write Mix")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT_DIR / "endurance.pdf"
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Endurance plot saved to {output_path}")
    return fig


if __name__ == "__main__":
    plot_endurance()
