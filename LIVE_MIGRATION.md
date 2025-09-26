# CXLSSDEval Live Migration Framework

## Overview

The CXLSSDEval Live Migration Framework enables seamless migration of WebAssembly workloads between x86_64 and ARM64 architectures based on system resource conditions. The framework monitors page cache usage, CPU temperature, and memory pressure to automatically trigger migrations when thresholds are exceeded.

## Features

- **Cross-ISA Migration**: Migrate WASM workloads between x86_64 and ARM64 architectures
- **Automatic Triggering**: Migration triggers based on:
  - Page cache exceeding threshold (default: 8GB)
  - CPU temperature exceeding threshold (default: 80°C)
  - Memory pressure exceeding 85%
- **Lightweight Checkpointing**: Uses MVVM (WebAssembly Live Migration) for efficient state serialization
- **Network Transfer**: TCP-based state transfer between machines
- **Seamless Execution**: Tasks resume execution on target machine without data loss

## Architecture

```
┌──────────────────────────┐         ┌──────────────────────────┐
│    x86_64 Source Host    │         │    ARM64 Target Host     │
├──────────────────────────┤         ├──────────────────────────┤
│                          │         │                          │
│  ┌──────────────────┐    │         │    ┌──────────────────┐  │
│  │  WasmScheduler   │    │         │    │  WasmScheduler   │  │
│  ├──────────────────┤    │         │    ├──────────────────┤  │
│  │ SystemMonitor    │────┼─Trigger─┼───>│ MigrationCoord.  │  │
│  │ - Page Cache     │    │         │    │ - TCP Listener   │  │
│  │ - Temperature    │    │         │    │ - State Restore  │  │
│  │ - Memory Press.  │    │         │    └──────────────────┘  │
│  └──────────────────┘    │         │                          │
│                          │  TCP    │                          │
│  ┌──────────────────┐    │ Transfer│    ┌──────────────────┐  │
│  │   WASM Task      │────┼────────>│    │   WASM Task      │  │
│  │ (Checkpointed)   │    │  State  │    │   (Restored)     │  │
│  └──────────────────┘    │         │    └──────────────────┘  │
└──────────────────────────┘         └──────────────────────────┘
```

## Building

### Prerequisites

- C++20 compatible compiler
- CMake 3.14+
- For ARM cross-compilation: `aarch64-linux-gnu-gcc`

```bash
# Install ARM cross-compiler (Ubuntu/Debian)
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### Build Instructions

```bash
# Use the provided build script
chmod +x build_migration.sh
./build_migration.sh

# Or build manually:

# Build for x86_64
mkdir build_x86 && cd build_x86
cmake .. -DCXL_ENABLE_MVVM=ON -DBUILD_ARM_TARGET=OFF
make wasm_scheduler

# Build for ARM64 (cross-compile)
mkdir build_arm && cd build_arm
cmake .. -DCXL_ENABLE_MVVM=ON -DBUILD_ARM_TARGET=ON \
         -DCMAKE_TOOLCHAIN_FILE=../arm_toolchain.cmake
make wasm_scheduler
```

## Usage

### Environment Variables

Configure migration behavior through environment variables:

```bash
# Migration thresholds
export MIGRATION_CACHE_THRESHOLD_MB=8192  # Page cache threshold (MB)
export MIGRATION_TEMP_THRESHOLD_C=80      # CPU temperature threshold (°C)

# Network configuration
export MIGRATION_PORT=9876                # TCP port for migration
export ARM_MIGRATION_HOST=192.168.1.100   # ARM target host IP
```

### Running the Migration System

#### On x86_64 Source Machine:

```bash
# Set ARM target host
export ARM_MIGRATION_HOST=<arm_host_ip>

# Run with your WASM module
./build_x86/test_migration_x86 <wasm_file>
```

#### On ARM64 Target Machine:

```bash
# The ARM binary will automatically listen for incoming migrations
./build_arm/test_migration_arm <wasm_file>
```

## API Integration

### Basic Usage in Your Application

```cpp
#include "wasm_scheduler.hpp"

int main() {
    using namespace cxl;

    // Configure environment
    setenv("MIGRATION_CACHE_THRESHOLD_MB", "4096", 1);
    setenv("ARM_MIGRATION_HOST", "192.168.1.100", 1);

    WasmScheduler scheduler;

    // Launch a WASM task
    WasmTaskDesc desc;
    desc.module_path = "workload.wasm";
    desc.entry = "main";

    int task_id = scheduler.launch(desc, TargetArch::X86_64);

    // Scheduler will automatically monitor and migrate
    // when thresholds are exceeded

    return 0;
}
```

### Programmatic Migration

```cpp
// Manually trigger migration
scheduler.migrate(task_id, TargetArch::ARM64);

// Check migration metrics
SystemMonitor monitor;
auto metrics = monitor.get_metrics();
if (monitor.should_migrate(metrics)) {
    scheduler.trigger_migration(metrics);
}
```

## Implementation Details

### Key Components

1. **SystemMonitor**: Monitors system metrics
   - Reads `/proc/meminfo` for page cache
   - Reads `/sys/class/thermal/` for CPU temperature
   - Uses `sysinfo()` for memory pressure

2. **MigrationCoordinator**: Handles network transfer
   - TCP socket-based communication
   - Binary state serialization
   - Automatic reconnection handling

3. **WasmScheduler**: Orchestrates migration
   - Task lifecycle management
   - Checkpoint/restore coordination
   - Cross-architecture task recreation

### State Serialization

The framework uses MVVM's lightweight checkpoint mechanism:
- Captures WASM execution state
- Serializes memory, stack, and registers
- Compresses state for network transfer
- Typical checkpoint size: 10-100MB depending on workload

## Monitoring and Debugging

Monitor the system through log output:

```
[WasmScheduler] Initializing live migration framework
[Monitor] Starting system monitoring thread
[Monitor] Cache: 5632.3 MB, Temp: 65.2°C, Memory: 72.1%
[Migration Trigger] Threshold exceeded!
  - Page Cache: 8234.1 MB (threshold: 8192)
[Migration] Checkpointing task 1 for migration...
[Migration] Successfully sent state to ARM host 192.168.1.100
```

## Performance Considerations

- **Migration Overhead**: 100-500ms for typical workloads
- **Network Bandwidth**: Requires 10-100 Mbps depending on state size
- **CPU Usage**: Minimal overhead (~1-2%) during monitoring
- **Memory Usage**: Checkpoint requires 2x task memory temporarily

## Troubleshooting

### Common Issues

1. **Migration fails to connect**
   - Check firewall rules for port 9876
   - Verify ARM_MIGRATION_HOST is correct
   - Ensure target machine is running receiver

2. **High false positive rate**
   - Adjust thresholds via environment variables
   - Increase monitoring interval in code

3. **State restore fails**
   - Ensure same WASM module on both machines
   - Check architecture compatibility flags

## Future Enhancements

- [ ] Multi-task migration prioritization
- [ ] Predictive migration based on trends
- [ ] GPU state migration support
- [ ] Container orchestration integration
- [ ] Live migration without stopping execution

## References

- [MVVM Project](https://github.com/Multi-V-VM/MVVM)
- [WebAssembly System Interface](https://wasi.dev/)
- [CXL Specification](https://computeexpresslink.org/)