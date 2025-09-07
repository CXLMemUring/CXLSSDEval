#!/usr/bin/env python3

"""
PMR Monitoring Script for CXL SSD
Monitors CXL Persistent Memory Region usage and performance
"""

import sys
import os
import time
import argparse
import json
from pathlib import Path

class PMRMonitor:
    def __init__(self, device_path):
        self.device_path = Path(device_path)
        self.stats = {
            'reads': [],
            'writes': [],
            'latency': [],
            'bandwidth': []
        }
        
    def check_device(self):
        """Check if CXL device exists and has PMR"""
        if not self.device_path.exists():
            print(f"Error: Device {self.device_path} not found")
            return False
            
        pmr_path = self.device_path / "pmr"
        if not pmr_path.exists():
            print(f"Error: PMR not found for device {self.device_path}")
            return False
            
        return True
    
    def read_stat(self, stat_name):
        """Read a statistic from sysfs"""
        stat_path = self.device_path / stat_name
        try:
            with open(stat_path, 'r') as f:
                return int(f.read().strip())
        except:
            return 0
    
    def monitor_once(self):
        """Perform one monitoring sample"""
        stats = {}
        
        # Read PMR statistics
        stats['pmr_size'] = self.read_stat('pmr_size')
        stats['pmr_used'] = self.read_stat('pmr_used')
        stats['pmr_reads'] = self.read_stat('pmr_reads')
        stats['pmr_writes'] = self.read_stat('pmr_writes')
        stats['pmr_read_latency_ns'] = self.read_stat('pmr_read_latency_ns')
        stats['pmr_write_latency_ns'] = self.read_stat('pmr_write_latency_ns')
        
        # Calculate utilization
        if stats['pmr_size'] > 0:
            stats['pmr_utilization'] = (stats['pmr_used'] / stats['pmr_size']) * 100
        else:
            stats['pmr_utilization'] = 0
            
        return stats
    
    def monitor_continuous(self, interval=1, duration=None):
        """Monitor continuously"""
        print(f"Monitoring PMR for device {self.device_path}")
        print("Press Ctrl+C to stop\n")
        
        # Header
        print(f"{'Time':<10} {'Util%':<8} {'Reads':<12} {'Writes':<12} "
              f"{'Read Lat(ns)':<15} {'Write Lat(ns)':<15}")
        print("-" * 80)
        
        start_time = time.time()
        last_reads = 0
        last_writes = 0
        
        try:
            while True:
                if duration and (time.time() - start_time) > duration:
                    break
                    
                stats = self.monitor_once()
                
                # Calculate deltas
                read_delta = stats['pmr_reads'] - last_reads
                write_delta = stats['pmr_writes'] - last_writes
                last_reads = stats['pmr_reads']
                last_writes = stats['pmr_writes']
                
                # Print current stats
                current_time = time.strftime("%H:%M:%S")
                print(f"{current_time:<10} {stats['pmr_utilization']:<8.1f} "
                      f"{read_delta:<12} {write_delta:<12} "
                      f"{stats['pmr_read_latency_ns']:<15} "
                      f"{stats['pmr_write_latency_ns']:<15}")
                
                # Store for analysis
                self.stats['reads'].append(read_delta)
                self.stats['writes'].append(write_delta)
                self.stats['latency'].append(stats['pmr_read_latency_ns'])
                
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("\nMonitoring stopped")
            
        self.print_summary()
    
    def print_summary(self):
        """Print monitoring summary"""
        if not self.stats['reads']:
            return
            
        print("\n" + "="*80)
        print("SUMMARY")
        print("="*80)
        
        # Calculate averages
        avg_reads = sum(self.stats['reads']) / len(self.stats['reads'])
        avg_writes = sum(self.stats['writes']) / len(self.stats['writes'])
        avg_latency = sum(self.stats['latency']) / len(self.stats['latency'])
        
        print(f"Average Reads/sec:  {avg_reads:.0f}")
        print(f"Average Writes/sec: {avg_writes:.0f}")
        print(f"Average Latency:    {avg_latency:.0f} ns")
        print(f"Total Samples:      {len(self.stats['reads'])}")
    
    def export_stats(self, output_file):
        """Export statistics to JSON file"""
        with open(output_file, 'w') as f:
            json.dump(self.stats, f, indent=2)
        print(f"Statistics exported to {output_file}")

def main():
    parser = argparse.ArgumentParser(description='Monitor CXL PMR statistics')
    parser.add_argument('--device', '-d', default='/sys/bus/cxl/devices/mem0',
                       help='CXL device path (default: /sys/bus/cxl/devices/mem0)')
    parser.add_argument('--interval', '-i', type=float, default=1.0,
                       help='Monitoring interval in seconds (default: 1.0)')
    parser.add_argument('--duration', '-t', type=int,
                       help='Monitoring duration in seconds (default: infinite)')
    parser.add_argument('--export', '-e',
                       help='Export statistics to JSON file')
    
    args = parser.parse_args()
    
    # Check if running as root
    if os.geteuid() != 0:
        print("Error: This script must be run as root")
        sys.exit(1)
    
    # Create monitor
    monitor = PMRMonitor(args.device)
    
    # Check device
    if not monitor.check_device():
        sys.exit(1)
    
    # Start monitoring
    monitor.monitor_continuous(args.interval, args.duration)
    
    # Export if requested
    if args.export:
        monitor.export_stats(args.export)

if __name__ == '__main__':
    main()