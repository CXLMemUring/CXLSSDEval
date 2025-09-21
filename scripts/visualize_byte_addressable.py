#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
import os

def load_results(results_dir):
    """Load the summary CSV file"""
    csv_path = os.path.join(results_dir, "summary.csv")
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found")
        return None

    df = pd.read_csv(csv_path)
    return df

def create_comparison_plots(df, output_dir):
    """Create comparison plots for byte-addressable I/O"""

    # Prepare data
    nvme_data = df[df['Device'] == 'NVMe'].sort_values('Size_Bytes')
    cxl_data = df[df['Device'] == 'CXL_DAX'].sort_values('Size_Bytes')

    sizes = nvme_data['Size_Bytes'].values

    # Create figure with 2x2 subplots
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Byte-Addressable I/O Performance: NVMe (FS+fdatasync) vs CXL (DAX)', fontsize=16)

    # 1. IOPS Comparison
    ax = axes[0, 0]
    x = np.arange(len(sizes))
    width = 0.35

    bars1 = ax.bar(x - width/2, nvme_data['IOPS'].values, width, label='NVMe', color='#2E86AB')
    bars2 = ax.bar(x + width/2, cxl_data['IOPS'].values, width, label='CXL DAX', color='#A23B72')

    ax.set_xlabel('I/O Size (Bytes)')
    ax.set_ylabel('IOPS')
    ax.set_title('IOPS Comparison')
    ax.set_xticks(x)
    ax.set_xticklabels(sizes)
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Add value labels on bars
    for bars in [bars1, bars2]:
        for bar in bars:
            height = bar.get_height()
            ax.annotate(f'{height:.0f}',
                       xy=(bar.get_x() + bar.get_width() / 2, height),
                       xytext=(0, 3),
                       textcoords="offset points",
                       ha='center', va='bottom', fontsize=8)

    # 2. Bandwidth Comparison
    ax = axes[0, 1]
    bars1 = ax.bar(x - width/2, nvme_data['BW_KBps'].values/1024, width, label='NVMe', color='#2E86AB')
    bars2 = ax.bar(x + width/2, cxl_data['BW_KBps'].values/1024, width, label='CXL DAX', color='#A23B72')

    ax.set_xlabel('I/O Size (Bytes)')
    ax.set_ylabel('Bandwidth (MB/s)')
    ax.set_title('Bandwidth Comparison')
    ax.set_xticks(x)
    ax.set_xticklabels(sizes)
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 3. Average Latency Comparison
    ax = axes[1, 0]
    ax.plot(sizes, nvme_data['Lat_usec'].values, 'o-', label='NVMe', color='#2E86AB', linewidth=2, markersize=8)
    ax.plot(sizes, cxl_data['Lat_usec'].values, 's-', label='CXL DAX', color='#A23B72', linewidth=2, markersize=8)

    ax.set_xlabel('I/O Size (Bytes)')
    ax.set_ylabel('Average Latency (μs)')
    ax.set_title('Average Latency Comparison')
    ax.set_xscale('log', base=2)
    ax.set_yscale('log')
    ax.legend()
    ax.grid(True, alpha=0.3, which="both")

    # 4. P99 Latency Comparison
    ax = axes[1, 1]
    ax.plot(sizes, nvme_data['P99_Lat_usec'].values, 'o-', label='NVMe', color='#2E86AB', linewidth=2, markersize=8)
    ax.plot(sizes, cxl_data['P99_Lat_usec'].values, 's-', label='CXL DAX', color='#A23B72', linewidth=2, markersize=8)

    ax.set_xlabel('I/O Size (Bytes)')
    ax.set_ylabel('P99 Latency (μs)')
    ax.set_title('P99 Latency Comparison')
    ax.set_xscale('log', base=2)
    ax.set_yscale('log')
    ax.legend()
    ax.grid(True, alpha=0.3, which="both")

    plt.tight_layout()

    # Save plot
    output_path = os.path.join(output_dir, 'byte_addressable_comparison.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved plot to {output_path}")

    # Create speedup plot
    fig, ax = plt.subplots(figsize=(10, 6))

    # Calculate speedup (CXL IOPS / NVMe IOPS)
    speedup = cxl_data['IOPS'].values / nvme_data['IOPS'].values

    bars = ax.bar(x, speedup, color='#4CAF50', edgecolor='black', linewidth=1.5)

    # Add value labels
    for i, (bar, val) in enumerate(zip(bars, speedup)):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.05,
               f'{val:.1f}x', ha='center', va='bottom', fontweight='bold')

    ax.axhline(y=1, color='red', linestyle='--', label='Equal Performance')
    ax.set_xlabel('I/O Size (Bytes)')
    ax.set_ylabel('Speedup (CXL DAX / NVMe)')
    ax.set_title('CXL DAX Speedup over NVMe for Byte-Addressable I/O')
    ax.set_xticks(x)
    ax.set_xticklabels(sizes)
    ax.legend()
    ax.grid(True, alpha=0.3)

    output_path = os.path.join(output_dir, 'byte_addressable_speedup.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved speedup plot to {output_path}")

    # Print summary statistics
    print("\n" + "="*60)
    print("Performance Summary")
    print("="*60)

    avg_speedup_iops = np.mean(speedup)
    avg_speedup_lat = np.mean(nvme_data['Lat_usec'].values / cxl_data['Lat_usec'].values)

    print(f"Average IOPS Speedup (CXL/NVMe): {avg_speedup_iops:.2f}x")
    print(f"Average Latency Improvement (NVMe/CXL): {avg_speedup_lat:.2f}x")

    print("\nPer-size speedup:")
    for size, sp in zip(sizes, speedup):
        print(f"  {size:4d}B: {sp:6.2f}x")

def main():
    if len(sys.argv) > 1:
        results_dir = sys.argv[1]
    else:
        results_dir = "results/byte_addressable"

    # Load results
    df = load_results(results_dir)
    if df is None:
        return 1

    # Create plots
    os.makedirs(results_dir, exist_ok=True)
    create_comparison_plots(df, results_dir)

    return 0

if __name__ == "__main__":
    sys.exit(main())