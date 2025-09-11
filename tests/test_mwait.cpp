#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include "../include/cxl_logger.hpp"
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <vector>

using namespace cxl_ssd;

// Test configuration from command line
struct TestConfig {
    std::string test_name = "basic";
    std::string device_path = "/sys/bus/cxl/devices/mem0";
    std::string cstate = "C1";
    int addresses = 1;
    int iterations = 1000;
    bool verbose = false;
};

// Parse command line arguments
TestConfig parse_args(int argc, char* argv[]) {
    TestConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--test" && i + 1 < argc) {
            config.test_name = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            config.device_path = argv[++i];
        } else if (arg == "--cstate" && i + 1 < argc) {
            config.cstate = argv[++i];
        } else if (arg == "--addresses" && i + 1 < argc) {
            config.addresses = std::stoi(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            config.iterations = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help") {
            CXL_LOG_INFO("Usage: {} [options]\n"
                     "Options:\n"
                     "  --test <name>       Test to run (basic, pmr_latency, cstate, batch, benchmark)\n"
                     "  --device <path>     CXL device path\n"
                     "  --cstate <state>    C-state to test (C0, C1, C2, C3, C6)\n"
                     "  --addresses <n>     Number of addresses for batch test\n"
                     "  --iterations <n>    Number of iterations for benchmark\n"
                     "  --verbose           Enable verbose output", argv[0]);
            exit(0);
        }
    }
    
    return config;
}

// Test basic MWAIT functionality
bool test_basic(const TestConfig& config) {
    CXL_LOG_INFO("Testing basic MWAIT functionality...");
    
    // Check CPU support
    if (!primitives::check_mwait_support()) {
        CXL_LOG_ERROR("MONITOR/MWAIT not supported on this CPU");
        return false;
    }
    CXL_LOG_INFO("✓ MONITOR/MWAIT supported");
    
    // Get max C-state
    uint32_t max_cstate = primitives::get_max_cstate();
    CXL_LOG_INFO_FMT("✓ Maximum C-state: C{}", max_cstate);
    
    // Create MWAIT object
    CXLMWait mwait;
    if (!mwait.initialize(config.device_path)) {
        CXL_LOG_ERROR_FMT("Failed to initialize: {}", mwait.get_last_error());
        return false;
    }
    CXL_LOG_INFO("✓ CXL device initialized");
    
    // Allocate test memory
    void* test_addr = utils::map_cxl_pmr(config.device_path, 0, 4096);
    if (!test_addr) {
        CXL_LOG_ERROR("Failed to map PMR");
        return false;
    }
    CXL_LOG_INFO_FMT("✓ PMR mapped at {}", static_cast<void*>(test_addr));
    
    // Set up monitoring
    MWaitConfig mconfig;
    mconfig.monitor_address = test_addr;
    mconfig.timeout_us = 1000000;  // 1 second
    mconfig.hint = MWaitHint::C1;
    mconfig.granularity = MonitorGranularity::CACHE_LINE;
    
    // Start writer thread
    std::atomic<bool> written(false);
    std::thread writer([test_addr, &written]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        *static_cast<volatile uint64_t*>(test_addr) = 0xDEADBEEF;
        written = true;
    });
    
    // Monitor and wait
    auto start = std::chrono::high_resolution_clock::now();
    MWaitStatus status = mwait.monitor_wait(mconfig);
    auto end = std::chrono::high_resolution_clock::now();
    
    writer.join();
    
    // Check result
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    CXL_LOG_INFO_FMT("Wait duration: {} µs", duration.count());
    
    if (status == MWaitStatus::SUCCESS && written) {
        CXL_LOG_INFO("✓ MWAIT woken by write");
    } else if (status == MWaitStatus::TIMEOUT) {
        CXL_LOG_ERROR("✗ MWAIT timed out");
        return false;
    } else {
        CXL_LOG_ERROR_FMT("✗ MWAIT failed: {}", static_cast<int>(status));
        return false;
    }
    
    // Clean up
    utils::unmap_cxl_pmr(test_addr, 4096);
    
    return true;
}

// Test PMR access latency
bool test_pmr_latency(const TestConfig& config) {
    CXL_LOG_INFO("Testing PMR access latency...");
    
    CXLMWait mwait;
    if (!mwait.initialize(config.device_path)) {
        CXL_LOG_ERROR_FMT("Failed to initialize: {}", mwait.get_last_error());
        return false;
    }
    
    void* pmr_addr = utils::map_cxl_pmr(config.device_path, 0, 1024 * 1024);  // 1MB
    if (!pmr_addr) {
        CXL_LOG_ERROR("Failed to map PMR");
        return false;
    }
    
    volatile uint64_t* ptr = static_cast<volatile uint64_t*>(pmr_addr);
    
    // Warm up
    for (int i = 0; i < 100; i++) {
        *ptr = i;
    }
    
    // Measure write latency
    std::vector<double> write_latencies;
    for (int i = 0; i < config.iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        *ptr = i;
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration<double, std::nano>(end - start);
        write_latencies.push_back(duration.count());
    }
    
    // Measure read latency
    std::vector<double> read_latencies;
    uint64_t dummy = 0;
    for (int i = 0; i < config.iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        dummy += *ptr;
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration<double, std::nano>(end - start);
        read_latencies.push_back(duration.count());
    }
    
    // Calculate statistics
    double avg_write = 0, avg_read = 0;
    for (auto lat : write_latencies) avg_write += lat;
    for (auto lat : read_latencies) avg_read += lat;
    avg_write /= write_latencies.size();
    avg_read /= read_latencies.size();
    
    CXL_LOG_INFO_FMT("Average write latency: {} ns", avg_write);
    CXL_LOG_INFO_FMT("Average read latency:  {} ns", avg_read);
    
    // Prevent optimization
    if (dummy == 0) CXL_LOG_TRACE("");
    
    utils::unmap_cxl_pmr(pmr_addr, 1024 * 1024);
    
    return true;
}

// Test different C-states
bool test_cstate(const TestConfig& config) {
    CXL_LOG_INFO_FMT("Testing C-state: {}", config.cstate);
    
    CXLMWait mwait;
    if (!mwait.initialize(config.device_path)) {
        CXL_LOG_ERROR_FMT("Failed to initialize: {}", mwait.get_last_error());
        return false;
    }
    
    void* test_addr = utils::map_cxl_pmr(config.device_path, 0, 4096);
    if (!test_addr) {
        CXL_LOG_ERROR("Failed to map PMR");
        return false;
    }
    
    // Map C-state string to enum
    MWaitHint hint = MWaitHint::C1;
    if (config.cstate == "C0") hint = MWaitHint::C0;
    else if (config.cstate == "C1") hint = MWaitHint::C1;
    else if (config.cstate == "C2") hint = MWaitHint::C2;
    else if (config.cstate == "C3") hint = MWaitHint::C3;
    else if (config.cstate == "C6") hint = MWaitHint::C6;
    
    MWaitConfig mconfig;
    mconfig.monitor_address = test_addr;
    mconfig.timeout_us = 100000;  // 100ms
    mconfig.hint = hint;
    
    // Measure power/latency trade-off
    std::vector<double> wake_latencies;
    
    for (int i = 0; i < 10; i++) {
        std::atomic<bool> ready(false);
        
        std::thread writer([test_addr, &ready, i]() {
            ready = true;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            *static_cast<volatile uint64_t*>(test_addr) = i;
        });
        
        while (!ready) std::this_thread::yield();
        
        auto start = std::chrono::high_resolution_clock::now();
        MWaitStatus status = mwait.monitor_wait(mconfig);
        auto end = std::chrono::high_resolution_clock::now();
        
        writer.join();
        
        if (status == MWaitStatus::SUCCESS) {
            auto duration = std::chrono::duration<double, std::micro>(end - start);
            wake_latencies.push_back(duration.count());
        }
    }
    
    // Calculate average wake latency
    double avg_latency = 0;
    for (auto lat : wake_latencies) avg_latency += lat;
    avg_latency /= wake_latencies.size();
    
    CXL_LOG_INFO_FMT("Average wake latency for {}: {} µs", config.cstate, avg_latency);
    
    utils::unmap_cxl_pmr(test_addr, 4096);
    
    return true;
}

// Test batch monitoring
bool test_batch(const TestConfig& config) {
    CXL_LOG_INFO_FMT("Testing batch monitoring with {} addresses...", config.addresses);
    
    CXLMWait mwait;
    if (!mwait.initialize(config.device_path)) {
        CXL_LOG_ERROR_FMT("Failed to initialize: {}", mwait.get_last_error());
        return false;
    }
    
    size_t page_size = 4096;
    size_t total_size = page_size * config.addresses;
    void* base_addr = utils::map_cxl_pmr(config.device_path, 0, total_size);
    if (!base_addr) {
        CXL_LOG_ERROR("Failed to map PMR");
        return false;
    }
    
    // Create multiple monitor configs
    std::vector<MWaitConfig> configs;
    for (int i = 0; i < config.addresses; i++) {
        MWaitConfig mconfig;
        mconfig.monitor_address = static_cast<char*>(base_addr) + (i * page_size);
        mconfig.timeout_us = 1000000;
        mconfig.hint = MWaitHint::C1;
        configs.push_back(mconfig);
    }
    
    // Start writer thread for random address
    int target_index = rand() % config.addresses;
    std::thread writer([base_addr, page_size, target_index]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        void* target = static_cast<char*>(base_addr) + (target_index * page_size);
        *static_cast<volatile uint64_t*>(target) = 0xCAFEBABE;
    });
    
    // Monitor batch
    auto start = std::chrono::high_resolution_clock::now();
    MWaitStatus status = mwait.monitor_wait_batch(configs);
    auto end = std::chrono::high_resolution_clock::now();
    
    writer.join();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    CXL_LOG_INFO_FMT("Batch monitor completed in {} µs", duration.count());
    CXL_LOG_INFO_FMT("Target address index: {}", target_index);
    
    utils::unmap_cxl_pmr(base_addr, total_size);
    
    return status == MWaitStatus::SUCCESS;
}

// Performance benchmark
bool test_benchmark(const TestConfig& config) {
    CXL_LOG_INFO_FMT("Running performance benchmark ({} iterations)...", config.iterations);
    
    CXLMWait mwait;
    if (!mwait.initialize(config.device_path)) {
        CXL_LOG_ERROR_FMT("Failed to initialize: {}", mwait.get_last_error());
        return false;
    }
    
    void* test_addr = utils::map_cxl_pmr(config.device_path, 0, 4096);
    if (!test_addr) {
        CXL_LOG_ERROR("Failed to map PMR");
        return false;
    }
    
    MWaitConfig mconfig;
    mconfig.monitor_address = test_addr;
    mconfig.timeout_us = 10000;  // 10ms timeout
    mconfig.hint = MWaitHint::C1;
    
    // Reset stats
    mwait.reset_stats();
    
    // Run benchmark
    auto bench_start = std::chrono::high_resolution_clock::now();
    
    std::atomic<bool> stop(false);
    std::thread writer([test_addr, &stop, &config]() {
        volatile uint64_t* ptr = static_cast<volatile uint64_t*>(test_addr);
        for (int i = 0; i < config.iterations && !stop; i++) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            *ptr = i;
        }
    });
    
    int successful_wakes = 0;
    for (int i = 0; i < config.iterations; i++) {
        MWaitStatus status = mwait.monitor_wait(mconfig);
        if (status == MWaitStatus::SUCCESS) {
            successful_wakes++;
        }
    }
    
    stop = true;
    writer.join();
    
    auto bench_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start);
    
    // Get statistics
    auto stats = mwait.get_stats();
    
    CXL_LOG_INFO("\nBenchmark Results:");
    CXL_LOG_INFO_FMT("  Total time:        {} ms", total_duration.count());
    CXL_LOG_INFO_FMT("  Total waits:       {}", stats.total_waits);
    CXL_LOG_INFO_FMT("  Successful wakes:  {}", stats.successful_wakes);
    CXL_LOG_INFO_FMT("  Timeouts:          {}", stats.timeouts);
    CXL_LOG_INFO_FMT("  Average wait time: {} ns", stats.avg_wait_time.count());
    CXL_LOG_INFO_FMT("  Throughput:        {} ops/sec", (stats.total_waits * 1000.0 / total_duration.count()));
    
    utils::unmap_cxl_pmr(test_addr, 4096);
    
    return true;
}

int main(int argc, char* argv[]) {
    TestConfig config = parse_args(argc, argv);
    
    // Set logging level
    if (config.verbose) {
        Logger::set_level(LogLevel::DEBUG_);
    } else {
        Logger::set_level(LogLevel::INFO);
    }
    
    bool success = false;
    
    // Run selected test
    if (config.test_name == "basic") {
        success = test_basic(config);
    } else if (config.test_name == "pmr_latency") {
        success = test_pmr_latency(config);
    } else if (config.test_name == "cstate") {
        success = test_cstate(config);
    } else if (config.test_name == "batch") {
        success = test_batch(config);
    } else if (config.test_name == "benchmark") {
        success = test_benchmark(config);
    } else {
    CXL_LOG_ERROR_FMT("Unknown test: {}", config.test_name);
        return 1;
    }
    
    return success ? 0 : 1;
}