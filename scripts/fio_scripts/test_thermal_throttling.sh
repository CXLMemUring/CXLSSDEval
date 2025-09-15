#!/bin/bash

# Thermal Throttling Test Script
# Monitors temperature and performance during sustained workload

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Thermal Throttling"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/thermal_throttling"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to get NVMe temperature
get_nvme_temperature() {
    local device=$1
    # Extract device name without /dev/
    local dev_name=$(basename "$device")

    # Try smartctl first
    if command -v smartctl &> /dev/null; then
        local temp=$(smartctl -a "$device" 2>/dev/null | grep -i "^Temperature:" | awk '{print $2}')
        if [[ -n "$temp" ]]; then
            echo "$temp"
            return
        fi
    fi

    # Try nvme-cli
    if command -v nvme &> /dev/null; then
        local temp=$(nvme smart-log "$device" 2>/dev/null | grep -i "^temperature" | awk '{print $3}' | sed 's/C$//')
        if [[ -n "$temp" ]]; then
            echo "$temp"
            return
        fi
    fi

    # Try sysfs
    if [[ -f "/sys/class/nvme/${dev_name}/device/temperature" ]]; then
        local temp_mk=$(cat "/sys/class/nvme/${dev_name}/device/temperature")
        # Convert millikelvin to Celsius
        echo $((temp_mk / 1000 - 273))
        return
    fi

    echo "N/A"
}

# Function to run thermal throttling test
run_thermal_test() {
    local duration=${1:-600}  # Default 10 minutes
    local sample_interval=5   # Sample every 5 seconds

    log_message "Starting thermal throttling test for ${duration} seconds"

    # Output files
    local metrics_file="${RESULTS_DIR}/thermal_metrics.csv"
    local fio_output="${RESULTS_DIR}/thermal_workload.json"
    local temp_log="${RESULTS_DIR}/temperature.log"

    # Initialize CSV header
    echo "Time,Temperature_C,Bandwidth_MB/s,IOPS,Latency_us" > "$metrics_file"

    # Start FIO in background with time-series logging
    log_message "Starting sustained workload..."
    fio --name=thermal_test \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw=randrw \
        --rwmixread=70 \
        --bs=4k \
        --iodepth=32 \
        --numjobs=4 \
        --runtime="${duration}" \
        --time_based \
        --group_reporting \
        --output-format=json \
        --output="${fio_output}" \
        --write_bw_log="${RESULTS_DIR}/thermal_bw" \
        --write_iops_log="${RESULTS_DIR}/thermal_iops" \
        --write_lat_log="${RESULTS_DIR}/thermal_lat" \
        --log_avg_msec=5000 &

    local fio_pid=$!

    # Monitor temperature and performance
    local start_time=$(date +%s)
    local elapsed=0

    log_message "Monitoring temperature and performance..."

    while [[ $elapsed -lt $duration ]] && kill -0 $fio_pid 2>/dev/null; do
        local current_time=$(date +%s)
        elapsed=$((current_time - start_time))

        # Get temperature
        local temp=$(get_nvme_temperature "$DEVICE")
        echo "[$elapsed] Temperature: ${temp}°C" | tee -a "$temp_log"

        # Get current performance from FIO logs if available
        local bw="0"
        local iops="0"
        local lat="0"

        # Try to get latest metrics from FIO logs
        if [[ -f "${RESULTS_DIR}/thermal_bw_bw.1.log" ]]; then
            bw=$(tail -1 "${RESULTS_DIR}/thermal_bw_bw.1.log" 2>/dev/null | awk '{print $2}' || echo "0")
        fi

        if [[ -f "${RESULTS_DIR}/thermal_iops_iops.1.log" ]]; then
            iops=$(tail -1 "${RESULTS_DIR}/thermal_iops_iops.1.log" 2>/dev/null | awk '{print $2}' || echo "0")
        fi

        if [[ -f "${RESULTS_DIR}/thermal_lat_lat.1.log" ]]; then
            lat=$(tail -1 "${RESULTS_DIR}/thermal_lat_lat.1.log" 2>/dev/null | awk '{print $2}' || echo "0")
        fi

        # Append to CSV
        echo "${elapsed},${temp},${bw},${iops},${lat}" >> "$metrics_file"

        # Check for thermal throttling
        if [[ "$temp" != "N/A" ]] && [[ "$temp" -ge 70 ]]; then
            log_message "WARNING: High temperature detected: ${temp}°C - Thermal throttling likely active"
        fi

        sleep "$sample_interval"
    done

    # Wait for FIO to complete
    wait $fio_pid
    local fio_exit_code=$?

    if [[ $fio_exit_code -eq 0 ]]; then
        log_message "Thermal test completed successfully"
    else
        log_message "FIO exited with code: $fio_exit_code"
    fi

    return $fio_exit_code
}

# Function to generate thermal analysis report
generate_thermal_report() {
    local report_file="${RESULTS_DIR}/thermal_analysis.txt"

    cat > "$report_file" << EOF
===============================================
Thermal Throttling Test Analysis
===============================================
Test Date: $(date)
Device: ${DEVICE}

Test Configuration:
- Workload: 70/30 Random Read/Write Mix
- Block Size: 4KB
- Queue Depth: 32
- Jobs: 4
- Duration: Sustained load test

Temperature Analysis:
EOF

    if [[ -f "${RESULTS_DIR}/temperature.log" ]]; then
        echo -e "\nTemperature Statistics:" >> "$report_file"

        # Extract temperatures and calculate stats
        local temps=$(grep "Temperature:" "${RESULTS_DIR}/temperature.log" | awk '{print $3}' | sed 's/°C//')

        if [[ -n "$temps" ]]; then
            local min_temp=$(echo "$temps" | sort -n | head -1)
            local max_temp=$(echo "$temps" | sort -n | tail -1)
            local avg_temp=$(echo "$temps" | awk '{sum+=$1} END {printf "%.1f", sum/NR}')

            echo "  Minimum: ${min_temp}°C" >> "$report_file"
            echo "  Maximum: ${max_temp}°C" >> "$report_file"
            echo "  Average: ${avg_temp}°C" >> "$report_file"

            # Check for throttling events
            local throttle_count=$(echo "$temps" | awk '$1>=70' | wc -l)
            local total_samples=$(echo "$temps" | wc -l)
            local throttle_percent=$((throttle_count * 100 / total_samples))

            echo -e "\nThermal Throttling Events:" >> "$report_file"
            echo "  Samples above 70°C: ${throttle_count}/${total_samples} (${throttle_percent}%)" >> "$report_file"
        fi
    fi

    # Parse FIO results if available
    if [[ -f "${RESULTS_DIR}/thermal_workload.json" ]] && command -v jq &> /dev/null; then
        echo -e "\nPerformance Summary:" >> "$report_file"

        local read_bw=$(jq -r '.jobs[0].read.bw' "${RESULTS_DIR}/thermal_workload.json" 2>/dev/null || echo "N/A")
        local write_bw=$(jq -r '.jobs[0].write.bw' "${RESULTS_DIR}/thermal_workload.json" 2>/dev/null || echo "N/A")
        local read_iops=$(jq -r '.jobs[0].read.iops' "${RESULTS_DIR}/thermal_workload.json" 2>/dev/null || echo "N/A")
        local write_iops=$(jq -r '.jobs[0].write.iops' "${RESULTS_DIR}/thermal_workload.json" 2>/dev/null || echo "N/A")

        echo "  Average Read Bandwidth: ${read_bw} KB/s" >> "$report_file"
        echo "  Average Write Bandwidth: ${write_bw} KB/s" >> "$report_file"
        echo "  Average Read IOPS: ${read_iops}" >> "$report_file"
        echo "  Average Write IOPS: ${write_iops}" >> "$report_file"
    fi

    echo -e "\nDetailed metrics saved in: ${RESULTS_DIR}/thermal_metrics.csv" >> "$report_file"
    echo -e "\nPlot generation command:" >> "$report_file"
    echo "  python3 visualize_thermal.py --input ${RESULTS_DIR}/thermal_metrics.csv" >> "$report_file"

    log_message "Thermal analysis report generated: $report_file"
    cat "$report_file"
}

# Function to create Python visualization script
create_visualization_script() {
    cat > "${RESULTS_DIR}/visualize_thermal.py" << 'EOF'
#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import sys
import argparse

def plot_thermal_data(csv_file, output_file=None):
    """Generate thermal throttling visualization with dual y-axes"""

    # Read CSV data
    df = pd.read_csv(csv_file)

    # Create figure with dual y-axes
    fig, ax1 = plt.subplots(figsize=(12, 6))

    # Plot temperature on first y-axis
    color = 'tab:red'
    ax1.set_xlabel('Time (seconds)')
    ax1.set_ylabel('Temperature (°C)', color=color)
    ax1.plot(df['Time'], df['Temperature_C'], color=color, label='Temperature', linewidth=2)
    ax1.tick_params(axis='y', labelcolor=color)
    ax1.axhline(y=70, color='orange', linestyle='--', alpha=0.5, label='Throttle Threshold')
    ax1.grid(True, alpha=0.3)

    # Create second y-axis for throughput
    ax2 = ax1.twinx()
    color = 'tab:blue'
    ax2.set_ylabel('Throughput (MB/s)', color=color)
    ax2.plot(df['Time'], df['Bandwidth_MB/s']/1024, color=color, label='Throughput', linewidth=2)
    ax2.tick_params(axis='y', labelcolor=color)

    # Add title and legend
    plt.title('Thermal Throttling Impact on SSD Performance')

    # Combine legends
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper right')

    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Plot saved to: {output_file}")
    else:
        plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Visualize thermal throttling data')
    parser.add_argument('--input', required=True, help='Input CSV file')
    parser.add_argument('--output', help='Output plot file (PNG/PDF)')

    args = parser.parse_args()
    plot_thermal_data(args.input, args.output)
EOF

    chmod +x "${RESULTS_DIR}/visualize_thermal.py"
    log_message "Visualization script created: ${RESULTS_DIR}/visualize_thermal.py"
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME Test ====="

    # Check device
    check_device "$DEVICE" || exit 1

    # Check for temperature monitoring tools
    if ! command -v smartctl &> /dev/null && ! command -v nvme &> /dev/null; then
        log_message "WARNING: Neither smartctl nor nvme-cli found. Temperature monitoring may not work."
        log_message "Install with: sudo apt-get install smartmontools nvme-cli"
    fi

    # Run thermal test (10 minutes by default, or use first argument)
    local test_duration=${1:-600}
    run_thermal_test "$test_duration"

    # Generate report
    generate_thermal_report

    # Create visualization script
    create_visualization_script

    # Try to generate plot if Python and matplotlib are available
    if command -v python3 &> /dev/null; then
        if python3 -c "import matplotlib" 2>/dev/null; then
            log_message "Generating thermal visualization..."
            python3 "${RESULTS_DIR}/visualize_thermal.py" \
                --input "${RESULTS_DIR}/thermal_metrics.csv" \
                --output "${RESULTS_DIR}/thermal_plot.png" 2>/dev/null || \
                log_message "Failed to generate plot. Install matplotlib: pip3 install matplotlib pandas"
        else
            log_message "matplotlib not found. Install with: pip3 install matplotlib pandas"
        fi
    fi

    log_message "===== Completed $TEST_NAME Test ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi