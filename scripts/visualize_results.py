#!/usr/bin/env python3

"""
Visualization Script for CXL SSD Test Results
This script generates graphs and charts from test results using matplotlib and seaborn
"""

import json
import os
import sys
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
from typing import Dict, List, Tuple
import argparse

# Set style for better looking plots
plt.style.use('seaborn-v0_8-darkgrid')
sns.set_palette("husl")

class TestResultVisualizer:
    def __init__(self, results_dir: str, output_dir: str):
        self.results_dir = Path(results_dir)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
    def parse_fio_json(self, json_file: str) -> Dict:
        """Parse FIO JSON output file"""
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
            return data
        except Exception as e:
            print(f"Error parsing {json_file}: {e}")
            return None
    
    def extract_metrics(self, data: Dict) -> Dict:
        """Extract key metrics from FIO JSON data"""
        if not data or 'jobs' not in data:
            return None
        
        metrics = {}
        job = data['jobs'][0]  # Usually only one job per file
        
        # Extract read metrics
        if 'read' in job:
            read_data = job['read']
            metrics['read_iops'] = read_data.get('iops', 0)
            metrics['read_bw_mb'] = read_data.get('bw', 0) / 1024  # Convert to MB/s
            metrics['read_lat_mean_us'] = read_data.get('lat_ns', {}).get('mean', 0) / 1000
            
            # Extract percentiles
            percentiles = read_data.get('clat_ns', {}).get('percentile', {})
            metrics['read_p90_us'] = percentiles.get('90.000000', 0) / 1000
            metrics['read_p99_us'] = percentiles.get('99.000000', 0) / 1000
            metrics['read_p999_us'] = percentiles.get('99.900000', 0) / 1000
        
        # Extract write metrics
        if 'write' in job:
            write_data = job['write']
            metrics['write_iops'] = write_data.get('iops', 0)
            metrics['write_bw_mb'] = write_data.get('bw', 0) / 1024
            metrics['write_lat_mean_us'] = write_data.get('lat_ns', {}).get('mean', 0) / 1000
            
            # Extract percentiles
            percentiles = write_data.get('clat_ns', {}).get('percentile', {})
            metrics['write_p90_us'] = percentiles.get('90.000000', 0) / 1000
            metrics['write_p99_us'] = percentiles.get('99.000000', 0) / 1000
            metrics['write_p999_us'] = percentiles.get('99.900000', 0) / 1000
        
        return metrics
    
    def plot_qd_performance(self):
        """Plot Queue Depth vs Performance"""
        qd_files = glob.glob(str(self.results_dir / "raw/qd_thread/qd*_jobs1_*.json"))
        
        if not qd_files:
            print("No queue depth test results found")
            return
        
        qd_data = {'qd': [], 'read_iops': [], 'write_iops': [], 
                   'read_lat': [], 'write_lat': []}
        
        for file in qd_files:
            filename = os.path.basename(file)
            # Extract QD from filename (e.g., "qd32_jobs1_read.json")
            parts = filename.split('_')
            qd = int(parts[0].replace('qd', ''))
            test_type = parts[2].replace('.json', '')
            
            data = self.parse_fio_json(file)
            if data:
                metrics = self.extract_metrics(data)
                if metrics:
                    if test_type == 'read' and qd not in qd_data['qd']:
                        qd_data['qd'].append(qd)
                        qd_data['read_iops'].append(metrics.get('read_iops', 0))
                        qd_data['read_lat'].append(metrics.get('read_lat_mean_us', 0))
                    elif test_type == 'write':
                        idx = qd_data['qd'].index(qd) if qd in qd_data['qd'] else -1
                        if idx >= 0:
                            qd_data['write_iops'].append(metrics.get('write_iops', 0))
                            qd_data['write_lat'].append(metrics.get('write_lat_mean_us', 0))
        
        # Sort by QD
        sorted_indices = sorted(range(len(qd_data['qd'])), key=lambda i: qd_data['qd'][i])
        for key in qd_data:
            qd_data[key] = [qd_data[key][i] for i in sorted_indices if i < len(qd_data[key])]
        
        # Create plots
        fig, axes = plt.subplots(2, 2, figsize=(15, 10))
        
        # IOPS vs QD
        axes[0, 0].plot(qd_data['qd'], qd_data['read_iops'], 'o-', label='Read', linewidth=2, markersize=8)
        axes[0, 0].plot(qd_data['qd'], qd_data['write_iops'], 's-', label='Write', linewidth=2, markersize=8)
        axes[0, 0].set_xlabel('Queue Depth')
        axes[0, 0].set_ylabel('IOPS')
        axes[0, 0].set_title('IOPS vs Queue Depth')
        axes[0, 0].set_xscale('log', base=2)
        axes[0, 0].legend()
        axes[0, 0].grid(True)
        
        # Latency vs QD
        axes[0, 1].plot(qd_data['qd'], qd_data['read_lat'], 'o-', label='Read', linewidth=2, markersize=8)
        axes[0, 1].plot(qd_data['qd'], qd_data['write_lat'], 's-', label='Write', linewidth=2, markersize=8)
        axes[0, 1].set_xlabel('Queue Depth')
        axes[0, 1].set_ylabel('Latency (μs)')
        axes[0, 1].set_title('Mean Latency vs Queue Depth')
        axes[0, 1].set_xscale('log', base=2)
        axes[0, 1].legend()
        axes[0, 1].grid(True)
        
        # IOPS/QD Efficiency
        if len(qd_data['qd']) > 0:
            read_efficiency = [iops/qd for iops, qd in zip(qd_data['read_iops'], qd_data['qd'])]
            write_efficiency = [iops/qd for iops, qd in zip(qd_data['write_iops'], qd_data['qd'])]
            
            axes[1, 0].plot(qd_data['qd'], read_efficiency, 'o-', label='Read', linewidth=2, markersize=8)
            axes[1, 0].plot(qd_data['qd'], write_efficiency, 's-', label='Write', linewidth=2, markersize=8)
            axes[1, 0].set_xlabel('Queue Depth')
            axes[1, 0].set_ylabel('IOPS per QD')
            axes[1, 0].set_title('Queue Depth Efficiency')
            axes[1, 0].set_xscale('log', base=2)
            axes[1, 0].legend()
            axes[1, 0].grid(True)
        
        # Latency * QD (Little's Law)
        axes[1, 1].plot(qd_data['qd'], [lat*qd/1000 for lat, qd in zip(qd_data['read_lat'], qd_data['qd'])], 
                       'o-', label='Read', linewidth=2, markersize=8)
        axes[1, 1].plot(qd_data['qd'], [lat*qd/1000 for lat, qd in zip(qd_data['write_lat'], qd_data['qd'])], 
                       's-', label='Write', linewidth=2, markersize=8)
        axes[1, 1].set_xlabel('Queue Depth')
        axes[1, 1].set_ylabel('Latency × QD (ms)')
        axes[1, 1].set_title("Little's Law Validation")
        axes[1, 1].set_xscale('log', base=2)
        axes[1, 1].legend()
        axes[1, 1].grid(True)
        
        plt.suptitle('Queue Depth Performance Analysis', fontsize=16, fontweight='bold')
        plt.tight_layout()
        plt.savefig(self.output_dir / 'qd_performance.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"Saved queue depth performance plot to {self.output_dir / 'qd_performance.png'}")
    
    def plot_blocksize_performance(self):
        """Plot Block Size vs Performance"""
        bs_files = glob.glob(str(self.results_dir / "raw/blocksize/bs_*.json"))
        
        if not bs_files:
            print("No block size test results found")
            return
        
        bs_data = {'blocksize': [], 'read_bw': [], 'write_bw': [], 
                   'read_iops': [], 'write_iops': []}
        
        # Define block size order
        bs_order = ['512', '1k', '4k', '16k', '64k', '256k', '1m', '4m', '16m', '64m']
        bs_values = {'512': 0.5, '1k': 1, '4k': 4, '16k': 16, '64k': 64, 
                    '256k': 256, '1m': 1024, '4m': 4096, '16m': 16384, '64m': 65536}
        
        for bs in bs_order:
            read_file = self.results_dir / f"raw/blocksize/bs_{bs}_read.json"
            write_file = self.results_dir / f"raw/blocksize/bs_{bs}_write.json"
            
            if read_file.exists():
                data = self.parse_fio_json(read_file)
                if data:
                    metrics = self.extract_metrics(data)
                    if metrics:
                        bs_data['blocksize'].append(bs_values[bs])
                        bs_data['read_bw'].append(metrics.get('read_bw_mb', 0))
                        bs_data['read_iops'].append(metrics.get('read_iops', 0))
            
            if write_file.exists():
                data = self.parse_fio_json(write_file)
                if data:
                    metrics = self.extract_metrics(data)
                    if metrics and len(bs_data['write_bw']) < len(bs_data['blocksize']):
                        bs_data['write_bw'].append(metrics.get('write_bw_mb', 0))
                        bs_data['write_iops'].append(metrics.get('write_iops', 0))
        
        # Create plots
        fig, axes = plt.subplots(1, 2, figsize=(15, 6))
        
        # Bandwidth vs Block Size
        axes[0].plot(bs_data['blocksize'], bs_data['read_bw'], 'o-', label='Read', linewidth=2, markersize=8)
        axes[0].plot(bs_data['blocksize'], bs_data['write_bw'], 's-', label='Write', linewidth=2, markersize=8)
        axes[0].set_xlabel('Block Size (KB)')
        axes[0].set_ylabel('Bandwidth (MB/s)')
        axes[0].set_title('Bandwidth vs Block Size')
        axes[0].set_xscale('log', base=2)
        axes[0].legend()
        axes[0].grid(True)
        
        # IOPS vs Block Size
        axes[1].plot(bs_data['blocksize'], bs_data['read_iops'], 'o-', label='Read', linewidth=2, markersize=8)
        axes[1].plot(bs_data['blocksize'], bs_data['write_iops'], 's-', label='Write', linewidth=2, markersize=8)
        axes[1].set_xlabel('Block Size (KB)')
        axes[1].set_ylabel('IOPS')
        axes[1].set_title('IOPS vs Block Size')
        axes[1].set_xscale('log', base=2)
        axes[1].set_yscale('log')
        axes[1].legend()
        axes[1].grid(True)
        
        plt.suptitle('Block Size Performance Analysis', fontsize=16, fontweight='bold')
        plt.tight_layout()
        plt.savefig(self.output_dir / 'blocksize_performance.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"Saved block size performance plot to {self.output_dir / 'blocksize_performance.png'}")
    
    def plot_latency_percentiles(self):
        """Plot latency percentiles comparison"""
        pattern_files = glob.glob(str(self.results_dir / "raw/access_pattern/pattern_*.json"))
        
        if not pattern_files:
            print("No access pattern test results found")
            return
        
        patterns = []
        p90_read = []
        p99_read = []
        p999_read = []
        p90_write = []
        p99_write = []
        p999_write = []
        
        for file in pattern_files:
            filename = os.path.basename(file)
            pattern = filename.replace('pattern_', '').replace('.json', '')
            
            data = self.parse_fio_json(file)
            if data:
                metrics = self.extract_metrics(data)
                if metrics:
                    patterns.append(pattern)
                    
                    if 'read' in pattern or pattern == 'read':
                        p90_read.append(metrics.get('read_p90_us', 0))
                        p99_read.append(metrics.get('read_p99_us', 0))
                        p999_read.append(metrics.get('read_p999_us', 0))
                        p90_write.append(0)
                        p99_write.append(0)
                        p999_write.append(0)
                    else:
                        p90_write.append(metrics.get('write_p90_us', 0))
                        p99_write.append(metrics.get('write_p99_us', 0))
                        p999_write.append(metrics.get('write_p999_us', 0))
                        p90_read.append(0)
                        p99_read.append(0)
                        p999_read.append(0)
        
        # Create grouped bar chart
        fig, ax = plt.subplots(figsize=(12, 6))
        
        x = np.arange(len(patterns))
        width = 0.15
        
        # Plot bars
        bars1 = ax.bar(x - 2.5*width, p90_read, width, label='P90 Read', color='skyblue')
        bars2 = ax.bar(x - 1.5*width, p99_read, width, label='P99 Read', color='steelblue')
        bars3 = ax.bar(x - 0.5*width, p999_read, width, label='P99.9 Read', color='darkblue')
        bars4 = ax.bar(x + 0.5*width, p90_write, width, label='P90 Write', color='lightcoral')
        bars5 = ax.bar(x + 1.5*width, p99_write, width, label='P99 Write', color='indianred')
        bars6 = ax.bar(x + 2.5*width, p999_write, width, label='P99.9 Write', color='darkred')
        
        ax.set_xlabel('Access Pattern')
        ax.set_ylabel('Latency (μs)')
        ax.set_title('Latency Percentiles by Access Pattern')
        ax.set_xticks(x)
        ax.set_xticklabels(patterns)
        ax.legend()
        ax.set_yscale('log')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(self.output_dir / 'latency_percentiles.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"Saved latency percentiles plot to {self.output_dir / 'latency_percentiles.png'}")
    
    def plot_rwmix_performance(self):
        """Plot Read/Write Mix Performance"""
        rwmix_files = glob.glob(str(self.results_dir / "raw/rwmix/rwmix_*.json"))
        
        if not rwmix_files:
            print("No read/write mix test results found")
            return
        
        rwmix_data = {'read_pct': [], 'read_iops': [], 'write_iops': [], 
                      'total_iops': [], 'read_bw': [], 'write_bw': []}
        
        for file in rwmix_files:
            filename = os.path.basename(file)
            # Extract read percentage from filename
            parts = filename.replace('rwmix_r', '').replace('.json', '').split('_w')
            read_pct = int(parts[0])
            
            data = self.parse_fio_json(file)
            if data:
                metrics = self.extract_metrics(data)
                if metrics:
                    rwmix_data['read_pct'].append(read_pct)
                    read_iops = metrics.get('read_iops', 0)
                    write_iops = metrics.get('write_iops', 0)
                    rwmix_data['read_iops'].append(read_iops)
                    rwmix_data['write_iops'].append(write_iops)
                    rwmix_data['total_iops'].append(read_iops + write_iops)
                    rwmix_data['read_bw'].append(metrics.get('read_bw_mb', 0))
                    rwmix_data['write_bw'].append(metrics.get('write_bw_mb', 0))
        
        # Sort by read percentage
        sorted_indices = sorted(range(len(rwmix_data['read_pct'])), 
                               key=lambda i: rwmix_data['read_pct'][i])
        for key in rwmix_data:
            rwmix_data[key] = [rwmix_data[key][i] for i in sorted_indices]
        
        # Create plots
        fig, axes = plt.subplots(2, 2, figsize=(15, 10))
        
        # Stacked IOPS
        axes[0, 0].bar(rwmix_data['read_pct'], rwmix_data['read_iops'], 
                      label='Read IOPS', color='skyblue')
        axes[0, 0].bar(rwmix_data['read_pct'], rwmix_data['write_iops'], 
                      bottom=rwmix_data['read_iops'], label='Write IOPS', color='lightcoral')
        axes[0, 0].set_xlabel('Read Percentage (%)')
        axes[0, 0].set_ylabel('IOPS')
        axes[0, 0].set_title('Read/Write IOPS Distribution')
        axes[0, 0].legend()
        axes[0, 0].grid(True, alpha=0.3)
        
        # Total IOPS
        axes[0, 1].plot(rwmix_data['read_pct'], rwmix_data['total_iops'], 
                       'o-', linewidth=2, markersize=8, color='green')
        axes[0, 1].set_xlabel('Read Percentage (%)')
        axes[0, 1].set_ylabel('Total IOPS')
        axes[0, 1].set_title('Total IOPS vs Read/Write Mix')
        axes[0, 1].grid(True)
        
        # Bandwidth
        axes[1, 0].plot(rwmix_data['read_pct'], rwmix_data['read_bw'], 
                       'o-', label='Read BW', linewidth=2, markersize=8)
        axes[1, 0].plot(rwmix_data['read_pct'], rwmix_data['write_bw'], 
                       's-', label='Write BW', linewidth=2, markersize=8)
        axes[1, 0].set_xlabel('Read Percentage (%)')
        axes[1, 0].set_ylabel('Bandwidth (MB/s)')
        axes[1, 0].set_title('Bandwidth vs Read/Write Mix')
        axes[1, 0].legend()
        axes[1, 0].grid(True)
        
        # Total Bandwidth
        total_bw = [r + w for r, w in zip(rwmix_data['read_bw'], rwmix_data['write_bw'])]
        axes[1, 1].plot(rwmix_data['read_pct'], total_bw, 
                       'o-', linewidth=2, markersize=8, color='purple')
        axes[1, 1].set_xlabel('Read Percentage (%)')
        axes[1, 1].set_ylabel('Total Bandwidth (MB/s)')
        axes[1, 1].set_title('Total Bandwidth vs Read/Write Mix')
        axes[1, 1].grid(True)
        
        plt.suptitle('Read/Write Mix Performance Analysis', fontsize=16, fontweight='bold')
        plt.tight_layout()
        plt.savefig(self.output_dir / 'rwmix_performance.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"Saved read/write mix performance plot to {self.output_dir / 'rwmix_performance.png'}")
    
    def plot_comparison_summary(self):
        """Create a summary comparison chart"""
        # Collect data from different test types
        test_types = ['Raw Device', 'Filesystem', 'RocksDB']
        metrics = {'4K Random Read IOPS': [], '4K Random Write IOPS': [], 
                  'Sequential Read BW (MB/s)': [], 'Sequential Write BW (MB/s)': []}
        
        # Try to get data from each test type
        for test_type in test_types:
            if test_type == 'Raw Device':
                # Get raw device metrics
                read_file = self.results_dir / "raw/access_pattern/pattern_randread.json"
                write_file = self.results_dir / "raw/access_pattern/pattern_randwrite.json"
                seq_read_file = self.results_dir / "raw/access_pattern/pattern_read.json"
                seq_write_file = self.results_dir / "raw/access_pattern/pattern_write.json"
            elif test_type == 'Filesystem':
                read_file = self.results_dir / "filesystem/fs_pattern_randread.json"
                write_file = self.results_dir / "filesystem/fs_pattern_randwrite.json"
                seq_read_file = self.results_dir / "filesystem/fs_pattern_read.json"
                seq_write_file = self.results_dir / "filesystem/fs_pattern_write.json"
            else:
                # RocksDB doesn't have direct comparable metrics
                metrics['4K Random Read IOPS'].append(0)
                metrics['4K Random Write IOPS'].append(0)
                metrics['Sequential Read BW (MB/s)'].append(0)
                metrics['Sequential Write BW (MB/s)'].append(0)
                continue
            
            # Extract metrics for each test type
            for metric_name, file_path in [
                ('4K Random Read IOPS', read_file),
                ('4K Random Write IOPS', write_file),
                ('Sequential Read BW (MB/s)', seq_read_file),
                ('Sequential Write BW (MB/s)', seq_write_file)
            ]:
                if file_path.exists():
                    data = self.parse_fio_json(file_path)
                    if data:
                        extracted = self.extract_metrics(data)
                        if extracted:
                            if 'IOPS' in metric_name:
                                if 'Read' in metric_name:
                                    metrics[metric_name].append(extracted.get('read_iops', 0))
                                else:
                                    metrics[metric_name].append(extracted.get('write_iops', 0))
                            else:
                                if 'Read' in metric_name:
                                    metrics[metric_name].append(extracted.get('read_bw_mb', 0))
                                else:
                                    metrics[metric_name].append(extracted.get('write_bw_mb', 0))
                        else:
                            metrics[metric_name].append(0)
                    else:
                        metrics[metric_name].append(0)
                else:
                    metrics[metric_name].append(0)
        
        # Create comparison chart
        fig, ax = plt.subplots(figsize=(12, 8))
        
        x = np.arange(len(test_types))
        width = 0.2
        
        colors = ['skyblue', 'lightcoral', 'lightgreen', 'plum']
        
        for i, (metric, values) in enumerate(metrics.items()):
            offset = (i - 1.5) * width
            bars = ax.bar(x + offset, values, width, label=metric, color=colors[i])
            
            # Add value labels on bars
            for bar, value in zip(bars, values):
                if value > 0:
                    height = bar.get_height()
                    ax.text(bar.get_x() + bar.get_width()/2., height,
                           f'{value:.0f}', ha='center', va='bottom')
        
        ax.set_xlabel('Test Type')
        ax.set_ylabel('Performance')
        ax.set_title('Performance Comparison Summary')
        ax.set_xticks(x)
        ax.set_xticklabels(test_types)
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(self.output_dir / 'comparison_summary.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"Saved comparison summary plot to {self.output_dir / 'comparison_summary.png'}")
    
    def generate_all_plots(self):
        """Generate all available plots"""
        print("Generating visualization plots...")
        
        self.plot_qd_performance()
        self.plot_blocksize_performance()
        self.plot_latency_percentiles()
        self.plot_rwmix_performance()
        self.plot_comparison_summary()
        
        print(f"\nAll plots saved to {self.output_dir}")

def main():
    parser = argparse.ArgumentParser(description='Visualize CXL SSD test results')
    parser.add_argument('--results-dir', default='./results',
                       help='Directory containing test results (default: ./results)')
    parser.add_argument('--output-dir', default='./results/plots',
                       help='Directory to save plots (default: ./results/plots)')
    parser.add_argument('--plot-type', choices=['all', 'qd', 'blocksize', 'latency', 'rwmix', 'summary'],
                       default='all', help='Type of plot to generate')
    
    args = parser.parse_args()
    
    visualizer = TestResultVisualizer(args.results_dir, args.output_dir)
    
    if args.plot_type == 'all':
        visualizer.generate_all_plots()
    elif args.plot_type == 'qd':
        visualizer.plot_qd_performance()
    elif args.plot_type == 'blocksize':
        visualizer.plot_blocksize_performance()
    elif args.plot_type == 'latency':
        visualizer.plot_latency_percentiles()
    elif args.plot_type == 'rwmix':
        visualizer.plot_rwmix_performance()
    elif args.plot_type == 'summary':
        visualizer.plot_comparison_summary()

if __name__ == "__main__":
    main()