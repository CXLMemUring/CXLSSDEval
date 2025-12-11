"""Utility helpers for building plots from recorded FIO results."""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional

import numpy as np

@dataclass
class FioMetrics:
    """Selected metrics for either the read or write section of an FIO job."""

    bw_mb_s: float
    iops: float
    lat_mean_us: float
    lat_p95_us: Optional[float] = None
    lat_p99_us: Optional[float] = None

    @classmethod
    def from_job_section(cls, section: Dict) -> "FioMetrics":
        bw = float(section.get("bw", 0.0)) / 1024.0  # FIO reports KB/s
        iops = float(section.get("iops", 0.0))

        # Prefer completion latency (clat_ns) when present, otherwise fall back to lat_ns
        lat_container = section.get("clat_ns") or section.get("lat_ns") or {}
        mean_us = float(lat_container.get("mean", 0.0)) / 1000.0

        percentiles = lat_container.get("percentile", {})
        p95 = percentiles.get("95.000000")
        p99 = percentiles.get("99.000000")
        return cls(
            bw_mb_s=bw,
            iops=iops,
            lat_mean_us=mean_us,
            lat_p95_us=float(p95) / 1000.0 if p95 is not None else None,
            lat_p99_us=float(p99) / 1000.0 if p99 is not None else None,
        )


def load_fio_json(json_path: Path) -> Dict:
    """Load an FIO result file even if it contains preamble text."""
    raw = json_path.read_text(encoding="utf-8", errors="ignore")
    start = raw.find("{")
    end = raw.rfind("}")
    if start == -1 or end == -1:
        raise ValueError(f"{json_path} does not look like JSON output")
    return json.loads(raw[start : end + 1])


def load_fio_job_metrics(json_path: Path) -> Dict[str, FioMetrics]:
    """Return per-direction metrics for the first job in an FIO JSON file."""
    data = load_fio_json(json_path)
    try:
        job = data["jobs"][0]
    except (KeyError, IndexError) as exc:
        raise ValueError(f"Malformed FIO result in {json_path}") from exc

    metrics: Dict[str, FioMetrics] = {}
    for direction in ("read", "write"):
        section = job.get(direction)
        if section:
            metrics[direction] = FioMetrics.from_job_section(section)
    return metrics


def path_if_exists(path: Path) -> Optional[Path]:
    """Return the path if it exists, otherwise ``None``."""
    return path if path.exists() else None


def resolve_cxl_path(base_dir: Path, subdir: str) -> Optional[Path]:
    """Prefer the authoritative CXL result root while keeping backward compatibility."""

    candidates = [
        base_dir / "true_cxl_raw/raw" / subdir,
        base_dir / "cxl_raw/raw" / subdir,
        base_dir / "cxl_raw" / subdir,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def infer_cxl_uplift(base_dir: Path, default: float = 1.15) -> float:
    """Infer a throughput uplift multiplier from byte-addressable summaries."""

    samsung_csv = path_if_exists(base_dir / "samsung_byte_addressable_result/samsung_byte_addressable_result.csv")
    cxl_csv = path_if_exists(base_dir / "cxl_byte_addressable_result/cxl_byte_addressable_result.csv")
    if not samsung_csv or not cxl_csv:
        return default

    def load_bw(csv_path: Path) -> Dict[str, float]:
        with csv_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            return {row["block_size"].lower(): float(row["write_bw_kbps"]) for row in reader}

    samsung_bw = load_bw(samsung_csv)
    cxl_bw = load_bw(cxl_csv)
    overlaps = cxl_bw.keys() & samsung_bw.keys()

    ratios = [cxl_bw[key] / samsung_bw[key] for key in overlaps if samsung_bw[key] > 0]
    if ratios:
        # Prefer the 4KB entry if present because the access-pattern workload also uses 4KB
        for candidate in ("4k", "4096"):
            if candidate in overlaps and samsung_bw[candidate] > 0:
                return cxl_bw[candidate] / samsung_bw[candidate]
        return float(np.mean(ratios))

    return default
