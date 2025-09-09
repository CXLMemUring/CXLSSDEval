#!/usr/bin/env python3

"""
Result Validation Script for CXL SSD Tests
This script validates test results and checks for anomalies or errors
"""

import json
import os
import sys
import glob
import statistics
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import argparse
from datetime import datetime

class ResultValidator:
    def __init__(self, results_dir: str, thresholds: Dict = None):
        self.results_dir = Path(results_dir)
        self.errors = []
        self.warnings = []
        self.info = []
        
        # Default validation thresholds
        self.thresholds = thresholds or {
            'min_iops': 100,  # Minimum expected IOPS
            'max_latency_ms': 1000,  # Maximum acceptable latency in ms
            'latency_variance_threshold': 10,  # Max ratio between p99.9 and median
            'min_bandwidth_mb': 1,  # Minimum expected bandwidth in MB/s
            'error_rate_threshold': 0.01,  # 1% error rate threshold
            'min_runtime_seconds': 60,  # Minimum test runtime
        }
        
    def parse_fio_json(self, json_file: str) -> Optional[Dict]:
        """Parse FIO JSON output file"""
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
            return data
        except Exception as e:
            self.errors.append(f"Failed to parse {json_file}: {e}")
            return None
    
    def validate_fio_result(self, filepath: str) -> bool:
        """Validate a single FIO result file"""
        data = self.parse_fio_json(filepath)
        if not data:
            return False
        
        filename = os.path.basename(filepath)
        valid = True
        
        # Check if test completed successfully
        if 'jobs' not in data or len(data['jobs']) == 0:
            self.errors.append(f"{filename}: No job data found")
            return False
        
        job = data['jobs'][0]
        
        # Validate runtime
        runtime = job.get('elapsed', 0)
        if runtime < self.thresholds['min_runtime_seconds']:
            self.warnings.append(f"{filename}: Short runtime ({runtime}s < {self.thresholds['min_runtime_seconds']}s)")
        
        # Check for errors
        error_count = job.get('error', 0)
        if error_count > 0:
            self.errors.append(f"{filename}: Test reported {error_count} errors")
            valid = False
        
        # Validate read performance if present
        if 'read' in job:
            valid &= self._validate_io_stats(filename, job['read'], 'read')
        
        # Validate write performance if present
        if 'write' in job:
            valid &= self._validate_io_stats(filename, job['write'], 'write')
        
        # Check job status
        if job.get('job_runtime', 0) == 0:
            self.errors.append(f"{filename}: Job runtime is 0")
            valid = False
        
        return valid
    
    def _validate_io_stats(self, filename: str, io_data: Dict, io_type: str) -> bool:
        """Validate IO statistics for read or write"""
        valid = True
        
        # Check IOPS
        iops = io_data.get('iops', 0)
        if iops < self.thresholds['min_iops']:
            self.warnings.append(f"{filename}: Low {io_type} IOPS ({iops:.2f} < {self.thresholds['min_iops']})")
        
        # Check bandwidth
        bw_mb = io_data.get('bw', 0) / 1024  # Convert KB/s to MB/s
        if bw_mb < self.thresholds['min_bandwidth_mb']:
            self.warnings.append(f"{filename}: Low {io_type} bandwidth ({bw_mb:.2f} MB/s < {self.thresholds['min_bandwidth_mb']} MB/s)")
        
        # Check latency
        lat_ns = io_data.get('lat_ns', {})
        if lat_ns:
            mean_lat_ms = lat_ns.get('mean', 0) / 1_000_000  # Convert ns to ms
            max_lat_ms = lat_ns.get('max', 0) / 1_000_000
            
            if mean_lat_ms > self.thresholds['max_latency_ms']:
                self.warnings.append(f"{filename}: High {io_type} mean latency ({mean_lat_ms:.2f}ms > {self.thresholds['max_latency_ms']}ms)")
            
            if max_lat_ms > self.thresholds['max_latency_ms'] * 10:
                self.warnings.append(f"{filename}: Very high {io_type} max latency ({max_lat_ms:.2f}ms)")
        
        # Check latency percentiles
        clat_ns = io_data.get('clat_ns', {})
        if clat_ns and 'percentile' in clat_ns:
            percentiles = clat_ns['percentile']
            
            # Check for latency consistency
            p50 = percentiles.get('50.000000', 0) / 1_000_000  # median in ms
            p99 = percentiles.get('99.000000', 0) / 1_000_000
            p999 = percentiles.get('99.900000', 0) / 1_000_000
            
            if p50 > 0:
                # Check variance between percentiles
                if p999 / p50 > self.thresholds['latency_variance_threshold']:
                    self.warnings.append(f"{filename}: High {io_type} latency variance (P99.9/P50 = {p999/p50:.2f})")
                
                # Check for latency spikes
                if p99 > p50 * 5:
                    self.info.append(f"{filename}: {io_type} latency spike detected (P99 = {p99:.2f}ms, P50 = {p50:.2f}ms)")
        
        # Check IO depth distribution
        iodepth_level = io_data.get('iodepth_level', {})
        if iodepth_level:
            # Check if IO depth was properly maintained
            depth_1 = iodepth_level.get('1', 0)
            if depth_1 > 95:  # More than 95% at depth 1
                self.info.append(f"{filename}: {io_type} mostly at IO depth 1 ({depth_1:.1f}%)")
        
        # Check for data integrity issues
        if io_data.get('total_ios', 0) == 0:
            self.errors.append(f"{filename}: No {io_type} IOs completed")
            valid = False
        
        return valid
    
    def validate_rocksdb_result(self, filepath: str) -> bool:
        """Validate RocksDB benchmark results"""
        filename = os.path.basename(filepath)
        valid = True
        
        try:
            with open(filepath, 'r') as f:
                content = f.read()
            
            # Check for common RocksDB errors
            if 'error' in content.lower() or 'failed' in content.lower():
                self.warnings.append(f"{filename}: Contains error messages")
            
            # Check for completion
            if 'ops/sec' not in content and 'micros/op' not in content:
                self.errors.append(f"{filename}: Missing performance metrics")
                valid = False
            
            # Extract and validate metrics
            lines = content.split('\n')
            for line in lines:
                if 'ops/sec' in line:
                    # Try to extract ops/sec value
                    try:
                        parts = line.split()
                        for i, part in enumerate(parts):
                            if 'ops/sec' in part and i > 0:
                                ops_sec = float(parts[i-1].replace(',', ''))
                                if ops_sec < 10:
                                    self.warnings.append(f"{filename}: Low ops/sec ({ops_sec})")
                    except:
                        pass
                
                if 'micros/op' in line:
                    # Try to extract latency
                    try:
                        parts = line.split()
                        for i, part in enumerate(parts):
                            if 'micros/op' in part and i > 0:
                                micros = float(parts[i-1].replace(',', ''))
                                if micros > 10000:  # 10ms
                                    self.warnings.append(f"{filename}: High latency ({micros} micros/op)")
                    except:
                        pass
            
        except Exception as e:
            self.errors.append(f"{filename}: Failed to read file: {e}")
            valid = False
        
        return valid
    
    def check_result_consistency(self, test_pattern: str) -> bool:
        """Check consistency across similar tests"""
        files = glob.glob(str(self.results_dir / test_pattern))
        
        if len(files) < 2:
            return True  # Can't check consistency with single file
        
        metrics_collection = {'iops': [], 'bw': [], 'lat': []}
        
        for filepath in files:
            data = self.parse_fio_json(filepath)
            if data and 'jobs' in data:
                job = data['jobs'][0]
                
                # Collect metrics from read or write
                for io_type in ['read', 'write']:
                    if io_type in job:
                        io_data = job[io_type]
                        metrics_collection['iops'].append(io_data.get('iops', 0))
                        metrics_collection['bw'].append(io_data.get('bw', 0))
                        lat_ns = io_data.get('lat_ns', {})
                        if lat_ns:
                            metrics_collection['lat'].append(lat_ns.get('mean', 0))
        
        # Check coefficient of variation for each metric
        for metric_name, values in metrics_collection.items():
            if len(values) > 1 and all(v > 0 for v in values):
                mean_val = statistics.mean(values)
                stdev_val = statistics.stdev(values)
                cv = (stdev_val / mean_val) * 100  # Coefficient of variation in %
                
                if cv > 50:  # More than 50% variation
                    self.warnings.append(f"{test_pattern}: High variation in {metric_name} (CV={cv:.1f}%)")
        
        return True
    
    def check_log_files(self) -> bool:
        """Check for errors in log files"""
        log_dir = self.results_dir.parent / 'logs'
        
        if not log_dir.exists():
            self.info.append("No log directory found")
            return True
        
        log_files = glob.glob(str(log_dir / '*.log'))
        
        for log_file in log_files:
            filename = os.path.basename(log_file)
            try:
                with open(log_file, 'r') as f:
                    content = f.read()
                
                # Check for common error patterns
                error_patterns = ['error', 'failed', 'abort', 'crash', 'timeout']
                for pattern in error_patterns:
                    if pattern in content.lower():
                        count = content.lower().count(pattern)
                        self.warnings.append(f"{filename}: Contains '{pattern}' ({count} occurrences)")
                
            except Exception as e:
                self.errors.append(f"{filename}: Failed to read log: {e}")
        
        return True
    
    def validate_test_completeness(self) -> bool:
        """Check if all expected tests were completed"""
        expected_tests = {
            'raw': [
                'qd_thread/qd*.json',
                'blocksize/bs_*.json',
                'access_pattern/pattern_*.json',
                'rwmix/rwmix_*.json',
                'distribution/dist_*.json',
                'endurance/endurance_*.json'
            ],
            'filesystem': [
                'fs_*.json'
            ],
            'rocksdb': [
                '*.log'
            ]
        }
        
        for test_type, patterns in expected_tests.items():
            test_dir = self.results_dir / test_type
            if not test_dir.exists():
                self.warnings.append(f"Missing {test_type} test results directory")
                continue
            
            for pattern in patterns:
                files = glob.glob(str(test_dir / pattern))
                if len(files) == 0:
                    self.warnings.append(f"No results found for {test_type}/{pattern}")
                else:
                    self.info.append(f"Found {len(files)} results for {test_type}/{pattern}")
        
        return True
    
    def generate_validation_report(self) -> str:
        """Generate a validation report"""
        report = []
        report.append("=" * 60)
        report.append("TEST RESULT VALIDATION REPORT")
        report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Results Directory: {self.results_dir}")
        report.append("=" * 60)
        report.append("")
        
        # Summary
        report.append("SUMMARY")
        report.append("-" * 30)
        report.append(f"Errors:   {len(self.errors)}")
        report.append(f"Warnings: {len(self.warnings)}")
        report.append(f"Info:     {len(self.info)}")
        report.append("")
        
        # Errors
        if self.errors:
            report.append("ERRORS (Must Fix)")
            report.append("-" * 30)
            for error in self.errors:
                report.append(f"  ✗ {error}")
            report.append("")
        
        # Warnings
        if self.warnings:
            report.append("WARNINGS (Should Review)")
            report.append("-" * 30)
            for warning in self.warnings:
                report.append(f"  ⚠ {warning}")
            report.append("")
        
        # Info
        if self.info:
            report.append("INFORMATION")
            report.append("-" * 30)
            for info in self.info:
                report.append(f"  ℹ {info}")
            report.append("")
        
        # Overall status
        report.append("OVERALL STATUS")
        report.append("-" * 30)
        if self.errors:
            report.append("❌ VALIDATION FAILED - Errors detected")
        elif self.warnings:
            report.append("⚠️  VALIDATION PASSED WITH WARNINGS")
        else:
            report.append("✅ VALIDATION PASSED")
        
        report.append("=" * 60)
        
        return "\n".join(report)
    
    def validate_all(self) -> bool:
        """Run all validation checks"""
        print("Starting validation...")
        
        # Validate FIO results
        fio_files = glob.glob(str(self.results_dir / "**/*.json"), recursive=True)
        for filepath in fio_files:
            self.validate_fio_result(filepath)
        
        # Validate RocksDB results
        rocksdb_files = glob.glob(str(self.results_dir / "rocksdb/*.log"))
        for filepath in rocksdb_files:
            self.validate_rocksdb_result(filepath)
        
        # Check consistency
        self.check_result_consistency("raw/qd_thread/qd*_jobs1_read.json")
        self.check_result_consistency("raw/blocksize/bs_*_read.json")
        
        # Check completeness
        self.validate_test_completeness()
        
        # Check logs
        self.check_log_files()
        
        # Return overall status
        return len(self.errors) == 0

def main():
    parser = argparse.ArgumentParser(description='Validate CXL SSD test results')
    parser.add_argument('--results-dir', default='./results',
                       help='Directory containing test results (default: ./results)')
    parser.add_argument('--output', help='Output file for validation report')
    parser.add_argument('--strict', action='store_true',
                       help='Treat warnings as errors')
    parser.add_argument('--thresholds', type=json.loads,
                       help='Custom validation thresholds as JSON')
    
    args = parser.parse_args()
    
    # Create validator
    validator = ResultValidator(args.results_dir, args.thresholds)
    
    # Run validation
    valid = validator.validate_all()
    
    # Generate report
    report = validator.generate_validation_report()
    
    # Print report
    print(report)
    
    # Save report if requested
    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"\nReport saved to {args.output}")
    
    # Exit with appropriate code
    if not valid or (args.strict and validator.warnings):
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()