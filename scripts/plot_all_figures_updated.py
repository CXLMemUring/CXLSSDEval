#!/usr/bin/env python3
"""
Master script to generate all plots for CXL SSD evaluation paper
All individual plotting functions have been moved to separate scripts for independence
"""

import matplotlib.pyplot as plt
from pathlib import Path

def main():
    """Generate all plots for the paper using individual scripts"""
    output_dir = Path('/home/huyp/CXLSSDEval/paper/img')
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Generating all plots for CXL SSD evaluation paper...")
    print("Note: All plots now use 16pt fonts for paper quality")

    # Import and run individual plot scripts
    scripts_to_run = [
        ('plot_byte_addressable', 'byte-addressable I/O performance'),
        ('plot_thermal_throttling', 'thermal throttling analysis'),
        ('plot_blocksize', 'block size comparison'),
        ('plot_qd_scalability', 'queue depth scalability'),
        ('plot_access_pattern', 'access pattern comparison'),
        ('plot_rwmix', 'read/write mix performance'),
        ('plot_compression_comparison', 'compression efficiency'),
        ('plot_pmr_latency_cdf', 'PMR latency distribution'),
        ('plot_cmb_bandwidth', 'CMB bandwidth utilization')
    ]

    successful_plots = []
    failed_plots = []

    for script_name, description in scripts_to_run:
        try:
            module = __import__(script_name)
            # Call the main plotting function (assumes same name as module)
            getattr(module, script_name)()
            print(f"✓ Generated {description}")
            successful_plots.append(description)
        except Exception as e:
            print(f"✗ Failed to generate {description}: {e}")
            failed_plots.append((description, str(e)))

    print(f"\nSummary:")
    print(f"✓ Successfully generated {len(successful_plots)} plots")
    if failed_plots:
        print(f"✗ Failed to generate {len(failed_plots)} plots:")
        for desc, error in failed_plots:
            print(f"  - {desc}: {error}")

    print(f"\nAll plots have been saved to {output_dir}")
    print("Generated files:")
    for pdf_file in sorted(output_dir.glob('*.pdf')):
        print(f"  - {pdf_file.name}")

if __name__ == "__main__":
    main()