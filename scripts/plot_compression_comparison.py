#!/usr/bin/env python3
"""
Generate compression ratio comparison plot for CXL SSD evaluation
"""

import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

def plot_compression_comparison():
    """Compression ratio and throughput impact comparison"""
    # Set matplotlib parameters for paper-quality figures
    plt.rcParams['font.size'] = 20
    plt.rcParams['axes.labelsize'] = 20
    plt.rcParams['axes.titlesize'] = 20
    plt.rcParams['xtick.labelsize'] = 20
    plt.rcParams['ytick.labelsize'] = 20
    plt.rcParams['legend.fontsize'] = 20
    plt.rcParams['figure.titlesize'] = 20

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    data_types = ['JSON', 'CSV', 'Binary', 'Encrypted', 'Text', 'Database']
    x_pos = np.arange(len(data_types))

    # Compression ratios based on paper specifications
    scaleflux_ratios = [3.8, 3.2, 2.1, 1.2, 3.5, 2.8]  # ScaleFlux: 3.8x (JSON), 3.2x (CSV), 1.2x (encrypted)
    samsung_ratios = [3.0, 2.8, 2.0, 1.1, 3.2, 2.5]    # Samsung: 2.5-3.0x with ZSTD
    cxl_ratios = [3.2, 3.0, 2.2, 1.3, 3.4, 2.7]        # CXL: adaptive compression

    # Throughput impact (percentage of baseline)
    scaleflux_throughput = [85, 88, 95, 98, 82, 86]     # 15% overhead for reads
    samsung_throughput = [92, 94, 96, 99, 90, 93]       # 8% overhead with workload optimization
    cxl_throughput = [95, 96, 97, 99, 94, 95]           # Adaptive based on CPU availability

    width = 0.25

    # Compression ratios subplot
    bars1 = ax1.bar(x_pos - width, scaleflux_ratios, width, label='ScaleFlux CSD1000', color='#ff7f0e')
    bars2 = ax1.bar(x_pos, samsung_ratios, width, label='Samsung SmartSSD', color='#1f77b4')
    bars3 = ax1.bar(x_pos + width, cxl_ratios, width, label='CXL SSD', alpha=0.7, hatch='//', color='#2ca02c')

    ax1.set_xlabel('Data Type', fontsize=20)
    ax1.set_ylabel('Compression Ratio', fontsize=20)
    ax1.set_title('(a) Compression Efficiency', fontsize=20)
    ax1.set_xticks(x_pos)
    ax1.set_xticklabels(data_types, rotation=45, fontsize=20)
    ax1.legend(fontsize=20)
    ax1.grid(True, alpha=0.3, axis='y')

    # Add ratio labels on bars
    for i, (sf, ss, cxl) in enumerate(zip(scaleflux_ratios, samsung_ratios, cxl_ratios)):
        if sf > 3.0:  # Only label high ratios
            ax1.text(i - width, sf + 0.1, f'{sf:.1f}×', ha='center', fontsize=20)
        if ss > 3.0:
            ax1.text(i, ss + 0.1, f'{ss:.1f}×', ha='center', fontsize=20)
        if cxl > 3.0:
            ax1.text(i + width, cxl + 0.1, f'{cxl:.1f}×', ha='center', fontsize=20)

    # Throughput impact subplot
    bars4 = ax2.bar(x_pos - width, scaleflux_throughput, width, label='ScaleFlux CSD1000', color='#ff7f0e')
    bars5 = ax2.bar(x_pos, samsung_throughput, width, label='Samsung SmartSSD', color='#1f77b4')
    bars6 = ax2.bar(x_pos + width, cxl_throughput, width, label='CXL SSD', alpha=0.7, hatch='//', color='#2ca02c')

    ax2.set_xlabel('Data Type', fontsize=20)
    ax2.set_ylabel('Throughput (% of baseline)', fontsize=20)
    ax2.set_title('(b) Compression Overhead', fontsize=20)
    ax2.set_xticks(x_pos)
    ax2.set_xticklabels(data_types, rotation=45, fontsize=20)
    ax2.legend(fontsize=20)
    ax2.grid(True, alpha=0.3, axis='y')
    ax2.set_ylim(75, 105)

    # Add baseline reference line
    ax2.axhline(y=100, color='gray', linestyle='--', alpha=0.5, linewidth=1)
    ax2.text(3, 101, 'No compression baseline', fontsize=20, alpha=0.7)

    plt.tight_layout()

    # Save the figure
    output_dir = Path(__file__).resolve().parents[2] / "img"
    output_dir.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_dir / 'compression_comparison.pdf', dpi=300, bbox_inches='tight')

    print(f"Compression comparison plot saved to {output_dir}/compression_comparison.pdf")

    return fig

if __name__ == "__main__":
    plot_compression_comparison()
    plt.show()
