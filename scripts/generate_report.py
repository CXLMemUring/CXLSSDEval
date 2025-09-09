#!/usr/bin/env python3

"""
Test Report Generation Script for CXL SSD Tests
This script generates comprehensive test reports with summaries and recommendations
"""

import json
import os
import sys
import glob
import pandas as pd
import numpy as np
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import argparse
from datetime import datetime
import statistics

class ReportGenerator:
    def __init__(self, results_dir: str, output_format: str = 'html'):
        self.results_dir = Path(results_dir)
        self.output_format = output_format
        self.test_data = {
            'raw': {},
            'filesystem': {},
            'rocksdb': {}
        }
        self.summary_metrics = {}
        
    def parse_fio_json(self, json_file: str) -> Optional[Dict]:
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
            return {}
        
        metrics = {}
        job = data['jobs'][0]
        
        # Extract read metrics
        if 'read' in job:
            read_data = job['read']
            metrics['read_iops'] = read_data.get('iops', 0)
            metrics['read_bw_mb'] = read_data.get('bw', 0) / 1024
            metrics['read_lat_mean_us'] = read_data.get('lat_ns', {}).get('mean', 0) / 1000
            
            # Extract percentiles
            percentiles = read_data.get('clat_ns', {}).get('percentile', {})
            metrics['read_p50_us'] = percentiles.get('50.000000', 0) / 1000
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
            metrics['write_p50_us'] = percentiles.get('50.000000', 0) / 1000
            metrics['write_p90_us'] = percentiles.get('90.000000', 0) / 1000
            metrics['write_p99_us'] = percentiles.get('99.000000', 0) / 1000
            metrics['write_p999_us'] = percentiles.get('99.900000', 0) / 1000
        
        return metrics
    
    def collect_test_data(self):
        """Collect all test data from result files"""
        # Collect raw device test data
        raw_files = glob.glob(str(self.results_dir / "raw/**/*.json"), recursive=True)
        for filepath in raw_files:
            data = self.parse_fio_json(filepath)
            if data:
                metrics = self.extract_metrics(data)
                if metrics:
                    test_name = os.path.basename(filepath).replace('.json', '')
                    self.test_data['raw'][test_name] = metrics
        
        # Collect filesystem test data
        fs_files = glob.glob(str(self.results_dir / "filesystem/*.json"))
        for filepath in fs_files:
            data = self.parse_fio_json(filepath)
            if data:
                metrics = self.extract_metrics(data)
                if metrics:
                    test_name = os.path.basename(filepath).replace('.json', '')
                    self.test_data['filesystem'][test_name] = metrics
        
        # Collect RocksDB test data (parse log files)
        rocksdb_files = glob.glob(str(self.results_dir / "rocksdb/*.log"))
        for filepath in rocksdb_files:
            test_name = os.path.basename(filepath).replace('.log', '')
            self.test_data['rocksdb'][test_name] = self.parse_rocksdb_log(filepath)
    
    def parse_rocksdb_log(self, log_file: str) -> Dict:
        """Parse RocksDB log file for metrics"""
        metrics = {}
        try:
            with open(log_file, 'r') as f:
                content = f.read()
            
            lines = content.split('\n')
            for line in lines:
                # Extract ops/sec
                if 'ops/sec' in line:
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if 'ops/sec' in part and i > 0:
                            try:
                                metrics['ops_per_sec'] = float(parts[i-1].replace(',', ''))
                            except:
                                pass
                
                # Extract micros/op
                if 'micros/op' in line:
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if 'micros/op' in part and i > 0:
                            try:
                                metrics['micros_per_op'] = float(parts[i-1].replace(',', ''))
                            except:
                                pass
                
                # Extract MB/s
                if 'MB/s' in line:
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if 'MB/s' in part and i > 0:
                            try:
                                metrics['throughput_mb'] = float(parts[i-1])
                            except:
                                pass
        except Exception as e:
            print(f"Error parsing RocksDB log {log_file}: {e}")
        
        return metrics
    
    def calculate_summary(self):
        """Calculate summary statistics across all tests"""
        # Raw device summary
        if self.test_data['raw']:
            raw_metrics = list(self.test_data['raw'].values())
            
            # Calculate averages for key metrics
            self.summary_metrics['raw'] = {
                'avg_read_iops': np.mean([m.get('read_iops', 0) for m in raw_metrics if m.get('read_iops', 0) > 0]),
                'avg_write_iops': np.mean([m.get('write_iops', 0) for m in raw_metrics if m.get('write_iops', 0) > 0]),
                'avg_read_bw_mb': np.mean([m.get('read_bw_mb', 0) for m in raw_metrics if m.get('read_bw_mb', 0) > 0]),
                'avg_write_bw_mb': np.mean([m.get('write_bw_mb', 0) for m in raw_metrics if m.get('write_bw_mb', 0) > 0]),
                'avg_read_lat_us': np.mean([m.get('read_lat_mean_us', 0) for m in raw_metrics if m.get('read_lat_mean_us', 0) > 0]),
                'avg_write_lat_us': np.mean([m.get('write_lat_mean_us', 0) for m in raw_metrics if m.get('write_lat_mean_us', 0) > 0]),
            }
            
            # Find peak performance
            self.summary_metrics['raw']['peak_read_iops'] = max([m.get('read_iops', 0) for m in raw_metrics], default=0)
            self.summary_metrics['raw']['peak_write_iops'] = max([m.get('write_iops', 0) for m in raw_metrics], default=0)
            self.summary_metrics['raw']['peak_read_bw_mb'] = max([m.get('read_bw_mb', 0) for m in raw_metrics], default=0)
            self.summary_metrics['raw']['peak_write_bw_mb'] = max([m.get('write_bw_mb', 0) for m in raw_metrics], default=0)
        
        # Filesystem summary
        if self.test_data['filesystem']:
            fs_metrics = list(self.test_data['filesystem'].values())
            self.summary_metrics['filesystem'] = {
                'avg_read_iops': np.mean([m.get('read_iops', 0) for m in fs_metrics if m.get('read_iops', 0) > 0]),
                'avg_write_iops': np.mean([m.get('write_iops', 0) for m in fs_metrics if m.get('write_iops', 0) > 0]),
                'overhead_pct': 0  # Will be calculated if raw data exists
            }
            
            # Calculate filesystem overhead
            if 'raw' in self.summary_metrics:
                raw_iops = self.summary_metrics['raw'].get('avg_read_iops', 0)
                fs_iops = self.summary_metrics['filesystem'].get('avg_read_iops', 0)
                if raw_iops > 0:
                    self.summary_metrics['filesystem']['overhead_pct'] = ((raw_iops - fs_iops) / raw_iops) * 100
        
        # RocksDB summary
        if self.test_data['rocksdb']:
            rocksdb_metrics = list(self.test_data['rocksdb'].values())
            self.summary_metrics['rocksdb'] = {
                'avg_ops_per_sec': np.mean([m.get('ops_per_sec', 0) for m in rocksdb_metrics if m.get('ops_per_sec', 0) > 0]),
                'avg_latency_us': np.mean([m.get('micros_per_op', 0) for m in rocksdb_metrics if m.get('micros_per_op', 0) > 0]),
            }
    
    def generate_recommendations(self) -> List[str]:
        """Generate optimization recommendations based on test results"""
        recommendations = []
        
        if not self.summary_metrics:
            return ["Insufficient data to generate recommendations"]
        
        # Raw device recommendations
        if 'raw' in self.summary_metrics:
            raw = self.summary_metrics['raw']
            
            # Check IOPS performance
            if raw.get('avg_read_iops', 0) < 10000:
                recommendations.append("• Low random read IOPS detected. Consider increasing queue depth or parallelism.")
            
            if raw.get('avg_write_iops', 0) < 5000:
                recommendations.append("• Low random write IOPS detected. Check device write cache settings.")
            
            # Check bandwidth
            if raw.get('peak_read_bw_mb', 0) < 1000:
                recommendations.append("• Sequential read bandwidth below 1GB/s. Verify PCIe link speed and width.")
            
            # Check latency
            if raw.get('avg_read_lat_us', 0) > 1000:
                recommendations.append("• High average read latency (>1ms). Consider enabling device-specific optimizations.")
        
        # Filesystem recommendations
        if 'filesystem' in self.summary_metrics:
            fs = self.summary_metrics['filesystem']
            
            if fs.get('overhead_pct', 0) > 20:
                recommendations.append(f"• Filesystem overhead is {fs['overhead_pct']:.1f}%. Consider using direct I/O or raw device for performance-critical workloads.")
        
        # RocksDB recommendations
        if 'rocksdb' in self.summary_metrics:
            rocksdb = self.summary_metrics['rocksdb']
            
            if rocksdb.get('avg_latency_us', 0) > 100:
                recommendations.append("• RocksDB operation latency is high. Consider tuning block cache size and compaction settings.")
            
            if rocksdb.get('avg_ops_per_sec', 0) < 10000:
                recommendations.append("• Low RocksDB throughput. Review write buffer size and memtable configuration.")
        
        # General recommendations
        recommendations.append("• Enable CPU affinity for I/O intensive threads to reduce context switching.")
        recommendations.append("• Consider using io_uring for better async I/O performance on Linux.")
        recommendations.append("• Monitor device temperature and throttling during sustained workloads.")
        
        return recommendations
    
    def generate_html_report(self) -> str:
        """Generate HTML format report"""
        html = []
        
        # HTML header
        html.append("""<!DOCTYPE html>
<html>
<head>
    <title>CXL SSD Test Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
        h1, h2, h3 { color: #333; }
        .header { background-color: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .section { background-color: white; padding: 20px; margin: 20px 0; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        table { border-collapse: collapse; width: 100%; margin: 15px 0; }
        th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }
        th { background-color: #3498db; color: white; }
        tr:nth-child(even) { background-color: #f2f2f2; }
        .metric { font-weight: bold; color: #2c3e50; }
        .good { color: #27ae60; }
        .warning { color: #f39c12; }
        .bad { color: #e74c3c; }
        .recommendation { background-color: #ecf0f1; padding: 15px; border-left: 4px solid #3498db; margin: 10px 0; }
        .summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; }
        .summary-card { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 20px; border-radius: 10px; }
        .summary-value { font-size: 2em; font-weight: bold; }
        .summary-label { font-size: 0.9em; opacity: 0.9; }
    </style>
</head>
<body>
""")
        
        # Header
        html.append(f"""
    <div class="header">
        <h1>CXL SSD Performance Test Report</h1>
        <p>Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
        <p>Results Directory: {self.results_dir}</p>
    </div>
""")
        
        # Executive Summary
        html.append("""
    <div class="section">
        <h2>Executive Summary</h2>
        <div class="summary-grid">
""")
        
        if 'raw' in self.summary_metrics:
            raw = self.summary_metrics['raw']
            html.append(f"""
            <div class="summary-card">
                <div class="summary-value">{raw.get('peak_read_iops', 0):,.0f}</div>
                <div class="summary-label">Peak Read IOPS</div>
            </div>
            <div class="summary-card">
                <div class="summary-value">{raw.get('peak_write_iops', 0):,.0f}</div>
                <div class="summary-label">Peak Write IOPS</div>
            </div>
            <div class="summary-card">
                <div class="summary-value">{raw.get('peak_read_bw_mb', 0):,.0f} MB/s</div>
                <div class="summary-label">Peak Read Bandwidth</div>
            </div>
            <div class="summary-card">
                <div class="summary-value">{raw.get('peak_write_bw_mb', 0):,.0f} MB/s</div>
                <div class="summary-label">Peak Write Bandwidth</div>
            </div>
""")
        
        html.append("""
        </div>
    </div>
""")
        
        # Raw Device Results
        if self.test_data['raw']:
            html.append("""
    <div class="section">
        <h2>Raw Device Test Results</h2>
        <table>
            <tr>
                <th>Test Name</th>
                <th>Read IOPS</th>
                <th>Write IOPS</th>
                <th>Read BW (MB/s)</th>
                <th>Write BW (MB/s)</th>
                <th>Read Lat (μs)</th>
                <th>Write Lat (μs)</th>
            </tr>
""")
            
            for test_name, metrics in sorted(self.test_data['raw'].items()):
                html.append(f"""
            <tr>
                <td>{test_name}</td>
                <td>{metrics.get('read_iops', 0):,.0f}</td>
                <td>{metrics.get('write_iops', 0):,.0f}</td>
                <td>{metrics.get('read_bw_mb', 0):,.1f}</td>
                <td>{metrics.get('write_bw_mb', 0):,.1f}</td>
                <td>{metrics.get('read_lat_mean_us', 0):,.1f}</td>
                <td>{metrics.get('write_lat_mean_us', 0):,.1f}</td>
            </tr>
""")
            
            html.append("""
        </table>
    </div>
""")
        
        # Filesystem Results
        if self.test_data['filesystem']:
            html.append("""
    <div class="section">
        <h2>Filesystem Test Results</h2>
        <table>
            <tr>
                <th>Test Name</th>
                <th>Read IOPS</th>
                <th>Write IOPS</th>
                <th>Read BW (MB/s)</th>
                <th>Write BW (MB/s)</th>
            </tr>
""")
            
            for test_name, metrics in sorted(self.test_data['filesystem'].items()):
                html.append(f"""
            <tr>
                <td>{test_name}</td>
                <td>{metrics.get('read_iops', 0):,.0f}</td>
                <td>{metrics.get('write_iops', 0):,.0f}</td>
                <td>{metrics.get('read_bw_mb', 0):,.1f}</td>
                <td>{metrics.get('write_bw_mb', 0):,.1f}</td>
            </tr>
""")
            
            html.append("""
        </table>
    </div>
""")
        
        # RocksDB Results
        if self.test_data['rocksdb']:
            html.append("""
    <div class="section">
        <h2>RocksDB Test Results</h2>
        <table>
            <tr>
                <th>Test Name</th>
                <th>Operations/sec</th>
                <th>Latency (μs)</th>
                <th>Throughput (MB/s)</th>
            </tr>
""")
            
            for test_name, metrics in sorted(self.test_data['rocksdb'].items()):
                html.append(f"""
            <tr>
                <td>{test_name}</td>
                <td>{metrics.get('ops_per_sec', 0):,.0f}</td>
                <td>{metrics.get('micros_per_op', 0):,.1f}</td>
                <td>{metrics.get('throughput_mb', 0):,.1f}</td>
            </tr>
""")
            
            html.append("""
        </table>
    </div>
""")
        
        # Recommendations
        recommendations = self.generate_recommendations()
        html.append("""
    <div class="section">
        <h2>Optimization Recommendations</h2>
""")
        
        for rec in recommendations:
            html.append(f'        <div class="recommendation">{rec}</div>\n')
        
        html.append("""
    </div>
""")
        
        # Footer
        html.append("""
</body>
</html>
""")
        
        return ''.join(html)
    
    def generate_markdown_report(self) -> str:
        """Generate Markdown format report"""
        md = []
        
        # Header
        md.append("# CXL SSD Performance Test Report\n")
        md.append(f"**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        md.append(f"**Results Directory:** {self.results_dir}\n\n")
        
        # Executive Summary
        md.append("## Executive Summary\n\n")
        
        if 'raw' in self.summary_metrics:
            raw = self.summary_metrics['raw']
            md.append("### Peak Performance Metrics\n\n")
            md.append(f"- **Peak Read IOPS:** {raw.get('peak_read_iops', 0):,.0f}\n")
            md.append(f"- **Peak Write IOPS:** {raw.get('peak_write_iops', 0):,.0f}\n")
            md.append(f"- **Peak Read Bandwidth:** {raw.get('peak_read_bw_mb', 0):,.0f} MB/s\n")
            md.append(f"- **Peak Write Bandwidth:** {raw.get('peak_write_bw_mb', 0):,.0f} MB/s\n")
            md.append(f"- **Average Read Latency:** {raw.get('avg_read_lat_us', 0):,.1f} μs\n")
            md.append(f"- **Average Write Latency:** {raw.get('avg_write_lat_us', 0):,.1f} μs\n\n")
        
        # Raw Device Results
        if self.test_data['raw']:
            md.append("## Raw Device Test Results\n\n")
            md.append("| Test Name | Read IOPS | Write IOPS | Read BW (MB/s) | Write BW (MB/s) | Read Lat (μs) | Write Lat (μs) |\n")
            md.append("|-----------|-----------|------------|----------------|-----------------|---------------|----------------|\n")
            
            for test_name, metrics in sorted(self.test_data['raw'].items()):
                md.append(f"| {test_name} | {metrics.get('read_iops', 0):,.0f} | {metrics.get('write_iops', 0):,.0f} | ")
                md.append(f"{metrics.get('read_bw_mb', 0):,.1f} | {metrics.get('write_bw_mb', 0):,.1f} | ")
                md.append(f"{metrics.get('read_lat_mean_us', 0):,.1f} | {metrics.get('write_lat_mean_us', 0):,.1f} |\n")
            md.append("\n")
        
        # Filesystem Results
        if self.test_data['filesystem']:
            md.append("## Filesystem Test Results\n\n")
            md.append("| Test Name | Read IOPS | Write IOPS | Read BW (MB/s) | Write BW (MB/s) |\n")
            md.append("|-----------|-----------|------------|----------------|------------------|\n")
            
            for test_name, metrics in sorted(self.test_data['filesystem'].items()):
                md.append(f"| {test_name} | {metrics.get('read_iops', 0):,.0f} | {metrics.get('write_iops', 0):,.0f} | ")
                md.append(f"{metrics.get('read_bw_mb', 0):,.1f} | {metrics.get('write_bw_mb', 0):,.1f} |\n")
            md.append("\n")
        
        # RocksDB Results
        if self.test_data['rocksdb']:
            md.append("## RocksDB Test Results\n\n")
            md.append("| Test Name | Operations/sec | Latency (μs) | Throughput (MB/s) |\n")
            md.append("|-----------|---------------|--------------|-------------------|\n")
            
            for test_name, metrics in sorted(self.test_data['rocksdb'].items()):
                md.append(f"| {test_name} | {metrics.get('ops_per_sec', 0):,.0f} | ")
                md.append(f"{metrics.get('micros_per_op', 0):,.1f} | {metrics.get('throughput_mb', 0):,.1f} |\n")
            md.append("\n")
        
        # Recommendations
        md.append("## Optimization Recommendations\n\n")
        recommendations = self.generate_recommendations()
        for rec in recommendations:
            md.append(f"{rec}\n")
        md.append("\n")
        
        # Performance Analysis
        md.append("## Performance Analysis\n\n")
        
        if 'filesystem' in self.summary_metrics and 'raw' in self.summary_metrics:
            overhead = self.summary_metrics['filesystem'].get('overhead_pct', 0)
            md.append(f"### Filesystem Overhead\n\n")
            md.append(f"The filesystem introduces approximately **{overhead:.1f}%** overhead compared to raw device performance.\n\n")
        
        md.append("### Test Coverage\n\n")
        md.append(f"- Raw device tests completed: {len(self.test_data.get('raw', {}))}\n")
        md.append(f"- Filesystem tests completed: {len(self.test_data.get('filesystem', {}))}\n")
        md.append(f"- RocksDB tests completed: {len(self.test_data.get('rocksdb', {}))}\n\n")
        
        return ''.join(md)
    
    def generate_csv_summary(self) -> pd.DataFrame:
        """Generate CSV summary of all test results"""
        rows = []
        
        # Add raw device results
        for test_name, metrics in self.test_data.get('raw', {}).items():
            row = {
                'test_type': 'raw',
                'test_name': test_name,
                'read_iops': metrics.get('read_iops', 0),
                'write_iops': metrics.get('write_iops', 0),
                'read_bw_mb': metrics.get('read_bw_mb', 0),
                'write_bw_mb': metrics.get('write_bw_mb', 0),
                'read_lat_us': metrics.get('read_lat_mean_us', 0),
                'write_lat_us': metrics.get('write_lat_mean_us', 0),
            }
            rows.append(row)
        
        # Add filesystem results
        for test_name, metrics in self.test_data.get('filesystem', {}).items():
            row = {
                'test_type': 'filesystem',
                'test_name': test_name,
                'read_iops': metrics.get('read_iops', 0),
                'write_iops': metrics.get('write_iops', 0),
                'read_bw_mb': metrics.get('read_bw_mb', 0),
                'write_bw_mb': metrics.get('write_bw_mb', 0),
                'read_lat_us': metrics.get('read_lat_mean_us', 0),
                'write_lat_us': metrics.get('write_lat_mean_us', 0),
            }
            rows.append(row)
        
        # Add RocksDB results
        for test_name, metrics in self.test_data.get('rocksdb', {}).items():
            row = {
                'test_type': 'rocksdb',
                'test_name': test_name,
                'ops_per_sec': metrics.get('ops_per_sec', 0),
                'latency_us': metrics.get('micros_per_op', 0),
                'throughput_mb': metrics.get('throughput_mb', 0),
            }
            rows.append(row)
        
        return pd.DataFrame(rows)
    
    def generate_report(self, output_file: Optional[str] = None):
        """Generate the complete report"""
        # Collect data
        print("Collecting test data...")
        self.collect_test_data()
        
        # Calculate summary
        print("Calculating summary statistics...")
        self.calculate_summary()
        
        # Generate report based on format
        if self.output_format == 'html':
            print("Generating HTML report...")
            report_content = self.generate_html_report()
            ext = '.html'
        elif self.output_format == 'markdown':
            print("Generating Markdown report...")
            report_content = self.generate_markdown_report()
            ext = '.md'
        else:
            print(f"Unknown format: {self.output_format}")
            return
        
        # Generate CSV summary
        csv_df = self.generate_csv_summary()
        
        # Save report
        if output_file:
            report_file = output_file
        else:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            report_file = f"test_report_{timestamp}{ext}"
        
        with open(report_file, 'w') as f:
            f.write(report_content)
        
        print(f"Report saved to: {report_file}")
        
        # Save CSV summary
        csv_file = report_file.replace(ext, '.csv')
        csv_df.to_csv(csv_file, index=False)
        print(f"CSV summary saved to: {csv_file}")
        
        # Print summary to console
        print("\n" + "="*60)
        print("TEST SUMMARY")
        print("="*60)
        
        if 'raw' in self.summary_metrics:
            print("\nRaw Device Performance:")
            for key, value in self.summary_metrics['raw'].items():
                if isinstance(value, float):
                    print(f"  {key}: {value:,.2f}")
                else:
                    print(f"  {key}: {value}")
        
        if 'filesystem' in self.summary_metrics:
            print("\nFilesystem Performance:")
            for key, value in self.summary_metrics['filesystem'].items():
                if isinstance(value, float):
                    print(f"  {key}: {value:,.2f}")
                else:
                    print(f"  {key}: {value}")
        
        if 'rocksdb' in self.summary_metrics:
            print("\nRocksDB Performance:")
            for key, value in self.summary_metrics['rocksdb'].items():
                if isinstance(value, float):
                    print(f"  {key}: {value:,.2f}")
                else:
                    print(f"  {key}: {value}")
        
        print("\n" + "="*60)

def main():
    parser = argparse.ArgumentParser(description='Generate test report for CXL SSD tests')
    parser.add_argument('--results-dir', default='./results',
                       help='Directory containing test results (default: ./results)')
    parser.add_argument('--format', choices=['html', 'markdown'], default='html',
                       help='Output format (default: html)')
    parser.add_argument('--output', help='Output file name')
    
    args = parser.parse_args()
    
    # Create report generator
    generator = ReportGenerator(args.results_dir, args.format)
    
    # Generate report
    generator.generate_report(args.output)

if __name__ == "__main__":
    main()