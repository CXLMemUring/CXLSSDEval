#!/usr/bin/env python3
"""Master script to generate all plots for the CXL SSD study."""

from __future__ import annotations

from importlib import import_module
from pathlib import Path
from typing import List, Tuple

import matplotlib.pyplot as plt

PLOTTERS: List[Tuple[str, str, str]] = [
    ("plot_byte_addressable", "plot_byte_addressable", "byte-addressable I/O performance"),
    ("plot_thermal_throttling", "plot_thermal_throttling", "thermal throttling analysis"),
    ("plot_blocksize", "plot_blocksize", "block-size comparison"),
    ("plot_access_pattern", "plot_access_pattern", "access pattern comparison"),
    ("plot_qd_scalability", "plot_qd_scalability", "queue-depth scalability"),
    ("plot_rwmix", "plot_rwmix", "read/write mix"),
    ("plot_compression_comparison", "plot_compression_comparison", "compression efficiency"),
    ("plot_pmr_latency_cdf", "plot_pmr_latency_cdf", "PMR latency CDF"),
    ("plot_cmb_bandwidth", "plot_cmb_bandwidth", "CMB bandwidth utilisation"),
]

OUTPUT_DIR = Path("/home/victoryang00/CXLSSDEval/paper/img")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print("Generating all plots for the CXL SSD evaluation paper...")

    successes: List[str] = []
    failures: List[Tuple[str, Exception]] = []

    for module_name, func_name, description in PLOTTERS:
        try:
            module = import_module(module_name)
            getattr(module, func_name)()
            successes.append(description)
            print(f"✓ Generated {description}")
        except Exception as exc:  # noqa: BLE001
            failures.append((description, exc))
            print(f"✗ Failed to generate {description}: {exc}")

    plt.close("all")

    print("\nSummary:")
    print(f"✓ Successfully generated {len(successes)} plots")
    if failures:
        print(f"✗ Failed to generate {len(failures)} plots:")
        for description, exc in failures:
            print(f"  - {description}: {exc}")

    print(f"\nAll plots have been saved to {OUTPUT_DIR}")
    print("Generated files:")
    for pdf_file in sorted(OUTPUT_DIR.glob("*.pdf")):
        print(f"  - {pdf_file.name}")


if __name__ == "__main__":
    main()
