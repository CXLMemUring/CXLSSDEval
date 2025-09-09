#!/usr/bin/env python3

import json
import csv
import os
import glob
from pathlib import Path

class FioResultParser:
    def __init__(self, results_dir):
        self.results_dir = results_dir
        self.summary_dir = os.path.join(results_dir, 'summary')
        os.makedirs(self.summary_dir, exist_ok=True)
    
    def parse_json_file(self, json_file):
        """Parse a single FIO JSON output file"""
        with open(json_file, 'r') as f:
            data = json.load(f)
        
        results = {}
        for job in data['jobs']:
            job_name = job['jobname']
            
            # Extract read metrics
            if 'read' in job:
                read_data = job['read']
                results[f"{job_name}_read"] = {
                    'iops': read_data['iops'],
                    'bw_mbps': read_data['bw'] / 1024,  # Convert to MB/s
                    'lat_p90': read_data['clat_ns']['percentile']['90.000000'] / 1000,  # Convert to us
                    'lat_p95': read_data['clat_ns']['percentile']['95.000000'] / 1000,
                    'lat_p99': read_data['clat_ns']['percentile']['99.000000'] / 1000,
                    'lat_p99.9': read_data['clat_ns']['percentile']['99.900000'] / 1000,
                    'lat_p99.99': read_data['clat_ns']['percentile']['99.990000'] / 1000,
                }
            
            # Extract write metrics
            if 'write' in job:
                write_data = job['write']
                results[f"{job_name}_write"] = {
                    'iops': write_data['iops'],
                    'bw_mbps': write_data['bw'] / 1024,
                    'lat_p90': write_data['clat_ns']['percentile']['90.000000'] / 1000,
                    'lat_p95': write_data['clat_ns']['percentile']['95.000000'] / 1000,
                    'lat_p99': write_data['clat_ns']['percentile']['99.000000'] / 1000,
                    'lat_p99.9': write_data['clat_ns']['percentile']['99.900000'] / 1000,
                    'lat_p99.99': write_data['clat_ns']['percentile']['99.990000'] / 1000,
                }
        
        return results
    
    def create_summary_csv(self, test_type):
        """Create CSV summary for a specific test type"""
        json_files = glob.glob(os.path.join(self.results_dir, test_type, '*.json'))
        
        if not json_files:
            print(f"No JSON files found for {test_type}")
            return
        
        csv_file = os.path.join(self.summary_dir, f"{test_type}_summary.csv")
        
        with open(csv_file, 'w', newline='') as f:
            writer = None
            
            for json_file in sorted(json_files):
                try:
                    results = self.parse_json_file(json_file)
                    test_name = Path(json_file).stem
                    
                    for job_name, metrics in results.items():
                        row = {'test_name': test_name, 'job': job_name}
                        row.update(metrics)
                        
                        if writer is None:
                            fieldnames = list(row.keys())
                            writer = csv.DictWriter(f, fieldnames=fieldnames)
                            writer.writeheader()
                        
                        writer.writerow(row)
                
                except Exception as e:
                    print(f"Error parsing {json_file}: {e}")
        
        print(f"Created summary CSV: {csv_file}")
    
    def generate_human_readable_report(self, test_type):
        """Generate human-readable report"""
        json_files = glob.glob(os.path.join(self.results_dir, test_type, '*.json'))
        
        report_file = os.path.join(self.summary_dir, f"{test_type}_report.txt")
        
        with open(report_file, 'w') as f:
            f.write(f"{'='*60}\n")
            f.write(f"Performance Report: {test_type.upper()}\n")
            f.write(f"{'='*60}\n\n")
            
            for json_file in sorted(json_files):
                try:
                    results = self.parse_json_file(json_file)
                    test_name = Path(json_file).stem
                    
                    f.write(f"\nTest: {test_name}\n")
                    f.write(f"{'-'*40}\n")
                    
                    for job_name, metrics in results.items():
                        f.write(f"\n  Job: {job_name}\n")
                        f.write(f"    IOPS: {metrics['iops']:.2f}\n")
                        f.write(f"    Throughput: {metrics['bw_mbps']:.2f} MB/s\n")
                        f.write(f"    Latency Percentiles (μs):\n")
                        f.write(f"      P90:    {metrics['lat_p90']:.2f}\n")
                        f.write(f"      P95:    {metrics['lat_p95']:.2f}\n")
                        f.write(f"      P99:    {metrics['lat_p99']:.2f}\n")
                        f.write(f"      P99.9:  {metrics['lat_p99.9']:.2f}\n")
                        f.write(f"      P99.99: {metrics['lat_p99.99']:.2f}\n")
                
                except Exception as e:
                    f.write(f"\nError parsing {json_file}: {e}\n")
        
        print(f"Created human-readable report: {report_file}")

def main():
    parser = FioResultParser('./results')
    
    # Process raw device tests
    parser.create_summary_csv('raw')
    parser.generate_human_readable_report('raw')
    
    # Process filesystem tests
    parser.create_summary_csv('filesystem')
    parser.generate_human_readable_report('filesystem')
    
    print("\nAll results parsed successfully!")

if __name__ == "__main__":
    main()