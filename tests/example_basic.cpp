#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace cxl = cxl_ssd;
using namespace cxl;

int main() {
    std::cout << "CXL SSD MWAIT Basic Example\n";
    std::cout << "===========================\n\n";
    
    // Check CPU support
    if (!primitives::check_mwait_support()) {
        std::cerr << "Error: MONITOR/MWAIT not supported on this CPU\n";
        return 1;
    }
    std::cout << "✓ CPU supports MONITOR/MWAIT\n";
    
    // Initialize CXL MWAIT
    CXLMWait mwait;
    std::string device_path = "/sys/bus/cxl/devices/mem0";
    
    if (!mwait.initialize(device_path)) {
        std::cerr << "Error: Failed to initialize CXL device: " 
                  << mwait.get_last_error() << "\n";
        return 1;
    }
    std::cout << "✓ CXL device initialized\n";
    
    // Map PMR memory
    void* pmr_addr = utils::map_cxl_pmr(device_path, 0, 4096);
    if (!pmr_addr) {
        std::cerr << "Error: Failed to map PMR\n";
        return 1;
    }
    std::cout << "✓ PMR mapped at address: " << pmr_addr << "\n\n";
    
    // Example 1: Simple wait for write
    std::cout << "Example 1: Waiting for memory write...\n";
    
    volatile uint64_t* watch_addr = static_cast<volatile uint64_t*>(pmr_addr);
    *watch_addr = 0;
    
    // Configure MWAIT
    MWaitConfig config;
    config.monitor_address = (void*)watch_addr;
    config.timeout_us = 5000000;  // 5 seconds
    config.hint = MWaitHint::C1;
    config.granularity = MonitorGranularity::CACHE_LINE;
    
    // Start a writer thread
    std::thread writer([watch_addr]() {
        std::cout << "  Writer: Sleeping for 2 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "  Writer: Writing to monitored address\n";
        *watch_addr = 0xDEADBEEF;
    });
    
    // Monitor and wait
    std::cout << "  Main: Starting MWAIT...\n";
    auto start = std::chrono::steady_clock::now();
    
    MWaitStatus status = mwait.monitor_wait(config);
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    writer.join();
    
    // Check result
    if (status == MWaitStatus::SUCCESS) {
        std::cout << "  ✓ Woken by write after " << duration.count() << " ms\n";
        std::cout << "  Value at address: 0x" << std::hex << *watch_addr << std::dec << "\n";
    } else if (status == MWaitStatus::TIMEOUT) {
        std::cout << "  ✗ Wait timed out\n";
    } else {
        std::cout << "  ✗ Wait failed\n";
    }
    
    std::cout << "\n";
    
    // Example 2: Wait with callback
    std::cout << "Example 2: Wait with callback function...\n";
    
    *watch_addr = 0;
    
    std::thread writer2([watch_addr]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        *watch_addr = 0xCAFEBABE;
    });
    
    status = mwait.monitor_wait_callback(config, []() {
        std::cout << "  ✓ Callback: Memory write detected!\n";
    });
    
    writer2.join();
    
    // Print statistics
    std::cout << "\nStatistics:\n";
    auto stats = mwait.get_stats();
    std::cout << "  Total waits:       " << stats.total_waits << "\n";
    std::cout << "  Successful wakes:  " << stats.successful_wakes << "\n";
    std::cout << "  Timeouts:          " << stats.timeouts << "\n";
    std::cout << "  Average wait time: " << stats.avg_wait_time.count() << " ns\n";
    
    // Cleanup
    utils::unmap_cxl_pmr(pmr_addr, 4096);
    
    std::cout << "\n✓ Example completed successfully\n";
    
    return 0;
}
