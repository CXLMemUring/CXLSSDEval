#!/usr/bin/env python3
"""
Comprehensive analysis of byte-addressable test results for Samsung and CSD SSDs
Extracts write, sync (fdatasync), and combined metrics from FIO JSON output
Generates separate CSV files for each device
"""

import json
import os
import sys
import csv
from pathlib import Path

def parse_fio_json(json_file):
    """Parse FIO JSON output and extract detailed metrics including sync operations"""
    try:
        with open(json_file, 'r') as f:
            data = json.load(f)

        jobs = data.get('jobs', [])
        if not jobs:
            return None

        job = jobs[0]

        # Extract write metrics
        write_data = job.get('write', {})
        # Extract sync metrics (fdatasync operations)
        sync_data = job.get('sync', {})

        metrics = {
            # Write operations
            'write_iops': write_data.get('iops', 0),
            'write_bw_kbps': write_data.get('bw', 0),
            'write_ios': write_data.get('total_ios', 0),
            'write_lat_mean_ns': write_data.get('clat_ns', {}).get('mean', 0),
            'write_lat_95_ns': write_data.get('clat_ns', {}).get('percentile', {}).get('95.000000', 0),
            'write_lat_99_ns': write_data.get('clat_ns', {}).get('percentile', {}).get('99.000000', 0),
            'write_lat_max_ns': write_data.get('clat_ns', {}).get('max', 0),

            # Sync (fdatasync) operations - using lat_ns instead of clat_ns
            'sync_iops': sync_data.get('iops', 0),
            'sync_ios': sync_data.get('total_ios', 0),
            'sync_lat_mean_ns': sync_data.get('lat_ns', {}).get('mean', 0),
            'sync_lat_95_ns': sync_data.get('lat_ns', {}).get('percentile', {}).get('95.000000', 0),
            'sync_lat_99_ns': sync_data.get('lat_ns', {}).get('percentile', {}).get('99.000000', 0),
            'sync_lat_max_ns': sync_data.get('lat_ns', {}).get('max', 0),
        }

        # Convert latencies from ns to us
        for key in list(metrics.keys()):
            if '_ns' in key:
                new_key = key.replace('_ns', '_us')
                metrics[new_key] = metrics[key] / 1000 if metrics[key] else 0

        # Calculate total operation latency (write + sync) - both average and 99p
        metrics['total_lat_avg_us'] = metrics['write_lat_mean_us'] + metrics['sync_lat_mean_us']
        metrics['total_lat_99_us'] = metrics['write_lat_99_us'] + metrics['sync_lat_99_us']

        return metrics

    except Exception as e:
        print(f"Error parsing {json_file}: {e}")
        return None

def format_number(num, decimal=2):
    """Format number with appropriate precision"""
    if num == 0:
        return "0"
    elif num < 1:
        return f"{num:.3f}"
    elif num < 100:
        return f"{num:.{decimal}f}"
    else:
        return f"{num:.0f}"

def analyze_device(device_name, results_dir):
    """Analyze byte-addressable results for a specific device"""

    # Block sizes in order
    block_sizes = ['8', '16', '32', '64', '128', '256', '512', '1k', '2k', '4k']

    print(f"\n{'='*100}")
    print(f"BYTE-ADDRESSABLE TEST ANALYSIS: {device_name.upper()}")
    print(f"{'='*100}")
    print(f"Results directory: {results_dir}")

    # Prepare CSV data
    csv_data = []

    # Check if directory exists
    if not os.path.exists(results_dir):
        print(f"Warning: Directory {results_dir} does not exist")
        return False

    # Print detailed header
    print(f"\n{'Block':<8} {'Write BW':<12} {'Write IOPS':<12} {'Write Avg':<12} {'Sync Avg':<12} {'Total Avg':<12} {'Write 99p':<12} {'Sync 99p':<12} {'Total 99p':<12}")
    print(f"{'Size':<8} {'(KB/s)':<12} {'(ops/s)':<12} {'Lat (us)':<12} {'Lat (us)':<12} {'Lat (us)':<12} {'(us)':<12} {'(us)':<12} {'(us)':<12}")
    print("-" * 110)

    for bs in block_sizes:
        json_file = os.path.join(results_dir, f"bs_{bs}_write.json")

        if not os.path.exists(json_file):
            print(f"Warning: {json_file} not found")
            continue

        metrics = parse_fio_json(json_file)

        if metrics:
            # Print to console
            print(f"{bs:<8} {format_number(metrics['write_bw_kbps']):<12} "
                  f"{format_number(metrics['write_iops']):<12} "
                  f"{format_number(metrics['write_lat_mean_us']):<12} "
                  f"{format_number(metrics['sync_lat_mean_us']):<12} "
                  f"{format_number(metrics['total_lat_avg_us']):<12} "
                  f"{format_number(metrics['write_lat_99_us']):<12} "
                  f"{format_number(metrics['sync_lat_99_us']):<12} "
                  f"{format_number(metrics['total_lat_99_us']):<12}")

            # Add to CSV data
            csv_data.append({
                'block_size': bs,
                'write_bw_kbps': metrics['write_bw_kbps'],
                'write_iops': metrics['write_iops'],
                'write_lat_avg_us': metrics['write_lat_mean_us'],
                'sync_lat_avg_us': metrics['sync_lat_mean_us'],
                'total_lat_avg_us': metrics['total_lat_avg_us'],
                'write_lat_99_us': metrics['write_lat_99_us'],
                'sync_lat_99_us': metrics['sync_lat_99_us'],
                'total_lat_99_us': metrics['total_lat_99_us']
            })

    # Write CSV file
    if csv_data:
        csv_file = os.path.join(results_dir, f"{device_name}_byte_addressable_summary.csv")
        with open(csv_file, 'w', newline='') as csvfile:
            fieldnames = ['block_size', 'write_bw_kbps', 'write_iops',
                         'write_lat_avg_us', 'sync_lat_avg_us', 'total_lat_avg_us',
                         'write_lat_99_us', 'sync_lat_99_us', 'total_lat_99_us']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            for row in csv_data:
                writer.writerow(row)

        print(f"\nCSV saved to: {csv_file}")

        # Print summary for paper
        print(f"\n{'='*100}")
        print(f"SUMMARY FOR PAPER - {device_name.upper()}")
        print(f"{'='*100}")
        print(f"\n{'Size':<8} {'BW(KB/s)':<12} {'Total Avg Lat(us)':<20} {'Total 99p Lat(us)':<20}")
        print("-" * 60)
        for row in csv_data:
            print(f"{row['block_size']:<8} {row['write_bw_kbps']:<12.0f} "
                  f"{row['total_lat_avg_us']:<20.2f} {row['total_lat_99_us']:<20.2f}")

        # Calculate performance degradation
        if len(csv_data) > 0:
            smallest = csv_data[0]  # 8 bytes
            baseline = next((r for r in csv_data if r['block_size'] == '512'), None)

            if baseline:
                bw_degradation = (1 - smallest['write_bw_kbps'] / baseline['write_bw_kbps']) * 100
                avg_lat_increase = (smallest['total_lat_avg_us'] / baseline['total_lat_avg_us'] - 1) * 100
                p99_lat_increase = (smallest['total_lat_99_us'] / baseline['total_lat_99_us'] - 1) * 100

                print(f"\n8-byte vs 512-byte comparison:")
                print(f"  - Bandwidth degradation: {bw_degradation:.1f}%")
                print(f"  - Average latency increase: {avg_lat_increase:.1f}%")
                print(f"  - 99th percentile latency increase: {p99_lat_increase:.1f}%")

                print(f"\nFdatasync overhead (as percentage of total latency):")
                for row in csv_data[:5]:  # First 5 sizes
                    sync_overhead_avg = row['sync_lat_avg_us'] / row['total_lat_avg_us'] * 100
                    sync_overhead_99p = row['sync_lat_99_us'] / row['total_lat_99_us'] * 100
                    print(f"  - {row['block_size']:<4}: Avg={sync_overhead_avg:.1f}%, 99p={sync_overhead_99p:.1f}%")

        return True
    else:
        print("No valid results found!")
        return False

def main():
    print("=" * 100)
    print("BYTE-ADDRESSABLE I/O PERFORMANCE ANALYSIS")
    print("Testing sub-512B writes with filesystem buffer I/O + fdatasync")
    print("=" * 100)

    # Analyze Samsung SSD
    samsung_dir = "/home/huyp/CXLSSDEval/scripts/samsung_byte_addressable_result"
    samsung_success = analyze_device("samsung", samsung_dir)

    # Analyze Scala SSD (CXL SSD with byte-addressable support)
    scala_dir = "/home/huyp/CXLSSDEval/scripts/scala_byte_addresable_result"
    scala_success = analyze_device("scala", scala_dir)

    # Analyze CSD SSD (if available)
    csd_dir = "/home/huyp/CXLSSDEval/scripts/csd_byte_addressable_result"
    csd_success = analyze_device("csd", csd_dir)

    # Final summary
    print("\n" + "=" * 100)
    print("ANALYSIS COMPLETE")
    print("=" * 100)

    if samsung_success:
        print(f"✓ Samsung results analyzed: {samsung_dir}/samsung_byte_addressable_summary.csv")
    else:
        print(f"✗ Samsung results not found or invalid")

    if scala_success:
        print(f"✓ Scala results analyzed: {scala_dir}/scala_byte_addressable_summary.csv")
    else:
        print(f"✗ Scala results not found or invalid")

    if csd_success:
        print(f"✓ CSD results analyzed: {csd_dir}/csd_byte_addressable_summary.csv")
    else:
        print(f"✗ CSD results not found or invalid")

    print("\nNOTE: Traditional NVMe SSDs require read-modify-write for sub-512B writes,")
    print("      while CXL SSDs with native byte-addressability can bypass these limitations.")

if __name__ == "__main__":
    main()