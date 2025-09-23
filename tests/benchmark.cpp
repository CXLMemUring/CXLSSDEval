#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace cxl = cxl_ssd;
using namespace cxl;

// Benchmark configuration
struct BenchmarkConfig {
    size_t num_threads = 1;
    size_t iterations = 10000;
    size_t pmr_size_mb = 16;
    bool quick = false;
    bool verbose = false;
};

// Benchmark results
struct BenchmarkResult {
    double avg_latency_ns;
    double min_latency_ns;
    double max_latency_ns;
    double p50_latency_ns;
    double p95_latency_ns;
    double p99_latency_ns;
    double throughput_ops_sec;
    size_t total_operations;
};

// Calculate percentile
double percentile(std::vector<double>& values, double p) {
    if (values.empty()) return 0.0;
    
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(p * values.size() / 100.0);
    if (index >= values.size()) index = values.size() - 1;
    
    return values[index];
}

// MWAIT latency benchmark
BenchmarkResult benchmark_mwait_latency(CXLMWait& mwait, void* pmr_addr, 
                                        const BenchmarkConfig& config) {
    std::cout << "Running MWAIT latency benchmark...\n";
    
    std::vector<double> latencies;
    latencies.reserve(config.iterations);
    
    volatile uint64_t* watch_addr = static_cast<volatile uint64_t*>(pmr_addr);
    
    MWaitConfig mconfig;
    mconfig.monitor_address = (void*)watch_addr;
    mconfig.timeout_us = 10000;  // 10ms timeout
    mconfig.hint = MWaitHint::C1;
    
    // Writer thread
    std::atomic<bool> stop(false);
    std::thread writer([watch_addr, &stop, &config]() {
        for (size_t i = 0; i < config.iterations && !stop; i++) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            *watch_addr = i;
        }
    });
    
    // Measure MWAIT latencies
    auto total_start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < config.iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        MWaitStatus status = mwait.monitor_wait(mconfig);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status == MWaitStatus::SUCCESS) {
            auto duration = std::chrono::duration<double, std::nano>(end - start);
            latencies.push_back(duration.count());
        }
        
        if (config.verbose && i % 1000 == 0) {
            std::cout << "  Progress: " << i << "/" << config.iterations << "\r" << std::flush;
        }
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration<double>(total_end - total_start);
    
    stop = true;
    writer.join();
    
    // Calculate statistics
    BenchmarkResult result;
    result.total_operations = latencies.size();
    
    if (!latencies.empty()) {
        result.avg_latency_ns = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        result.min_latency_ns = *std::min_element(latencies.begin(), latencies.end());
        result.max_latency_ns = *std::max_element(latencies.begin(), latencies.end());
        result.p50_latency_ns = percentile(latencies, 50);
        result.p95_latency_ns = percentile(latencies, 95);
        result.p99_latency_ns = percentile(latencies, 99);
        result.throughput_ops_sec = result.total_operations / total_duration.count();
    }
    
    return result;
}

// PMR read/write benchmark
BenchmarkResult benchmark_pmr_access(void* pmr_addr, size_t size, 
                                     const BenchmarkConfig& config) {
    std::cout << "Running PMR access benchmark...\n";
    
    std::vector<double> read_latencies;
    std::vector<double> write_latencies;
    read_latencies.reserve(config.iterations);
    write_latencies.reserve(config.iterations);
    
    volatile uint64_t* ptr = static_cast<volatile uint64_t*>(pmr_addr);
    size_t num_elements = size / sizeof(uint64_t);
    
    // Warm up
    for (size_t i = 0; i < 100; i++) {
        ptr[i % num_elements] = i;
    }
    
    auto total_start = std::chrono::high_resolution_clock::now();
    
    // Benchmark writes
    for (size_t i = 0; i < config.iterations; i++) {
        size_t index = i % num_elements;
        
        auto start = std::chrono::high_resolution_clock::now();
        ptr[index] = i;
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration<double, std::nano>(end - start);
        write_latencies.push_back(duration.count());
    }
    
    // Benchmark reads
    uint64_t dummy = 0;
    for (size_t i = 0; i < config.iterations; i++) {
        size_t index = i % num_elements;
        
        auto start = std::chrono::high_resolution_clock::now();
        dummy += ptr[index];
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration<double, std::nano>(end - start);
        read_latencies.push_back(duration.count());
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration<double>(total_end - total_start);
    
    // Prevent optimization
    if (dummy == 0) std::cout << "";
    
    // Combine read and write latencies
    std::vector<double> all_latencies;
    all_latencies.insert(all_latencies.end(), read_latencies.begin(), read_latencies.end());
    all_latencies.insert(all_latencies.end(), write_latencies.begin(), write_latencies.end());
    
    // Calculate statistics
    BenchmarkResult result;
    result.total_operations = all_latencies.size();
    result.avg_latency_ns = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
    result.min_latency_ns = *std::min_element(all_latencies.begin(), all_latencies.end());
    result.max_latency_ns = *std::max_element(all_latencies.begin(), all_latencies.end());
    result.p50_latency_ns = percentile(all_latencies, 50);
    result.p95_latency_ns = percentile(all_latencies, 95);
    result.p99_latency_ns = percentile(all_latencies, 99);
    result.throughput_ops_sec = result.total_operations / total_duration.count();
    
    return result;
}

// Multi-threaded MWAIT benchmark
BenchmarkResult benchmark_multithreaded(CXLMWait& mwait, void* pmr_addr, 
                                        const BenchmarkConfig& config) {
    std::cout << "Running multi-threaded benchmark (" << config.num_threads << " threads)...\n";
    
    std::vector<std::thread> threads;
    std::atomic<size_t> total_ops(0);
    std::atomic<bool> stop(false);
    
    size_t page_size = 4096;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Start worker threads
    for (size_t t = 0; t < config.num_threads; t++) {
        threads.emplace_back([&, t]() {
            void* thread_addr = static_cast<char*>(pmr_addr) + (t * page_size);
            volatile uint64_t* watch_addr = static_cast<volatile uint64_t*>(thread_addr);
            
            MWaitConfig mconfig;
            mconfig.monitor_address = (void*)watch_addr;
            mconfig.timeout_us = 1000;
            mconfig.hint = MWaitHint::C1;
            
            size_t local_ops = 0;
            
            // Writer for this thread
            std::thread writer([watch_addr, &stop]() {
                size_t counter = 0;
                while (!stop) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    *watch_addr = counter++;
                }
            });
            
            // Perform MWAIT operations
            size_t iterations_per_thread = config.iterations / config.num_threads;
            for (size_t i = 0; i < iterations_per_thread && !stop; i++) {
                MWaitStatus status = mwait.monitor_wait(mconfig);
                if (status == MWaitStatus::SUCCESS) {
                    local_ops++;
                }
            }
            
            stop = true;
            writer.join();
            
            total_ops += local_ops;
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time);
    
    BenchmarkResult result;
    result.total_operations = total_ops;
    result.throughput_ops_sec = total_ops / duration.count();
    result.avg_latency_ns = (duration.count() * 1e9) / total_ops;
    
    return result;
}

// Print results
void print_results(const std::string& name, const BenchmarkResult& result) {
    std::cout << "\n" << name << " Results:\n";
    std::cout << std::string(40, '-') << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Total operations:     " << result.total_operations << "\n";
    std::cout << "  Throughput:           " << result.throughput_ops_sec << " ops/sec\n";
    std::cout << "  Average latency:      " << result.avg_latency_ns << " ns\n";
    std::cout << "  Min latency:          " << result.min_latency_ns << " ns\n";
    std::cout << "  Max latency:          " << result.max_latency_ns << " ns\n";
    std::cout << "  P50 latency:          " << result.p50_latency_ns << " ns\n";
    std::cout << "  P95 latency:          " << result.p95_latency_ns << " ns\n";
    std::cout << "  P99 latency:          " << result.p99_latency_ns << " ns\n";
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoul(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            config.iterations = std::stoul(argv[++i]);
        } else if (arg == "--pmr-size" && i + 1 < argc) {
            config.pmr_size_mb = std::stoul(argv[++i]);
        } else if (arg == "--quick") {
            config.quick = true;
            config.iterations = 1000;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                     << "Options:\n"
                     << "  --threads <n>      Number of threads (default: 1)\n"
                     << "  --iterations <n>   Number of iterations (default: 10000)\n"
                     << "  --pmr-size <mb>    PMR size in MB (default: 16)\n"
                     << "  --quick            Quick benchmark (1000 iterations)\n"
                     << "  --verbose          Verbose output\n";
            return 0;
        }
    }
    
    std::cout << "CXL SSD MWAIT Performance Benchmark\n";
    std::cout << "====================================\n\n";
    
    std::cout << "Configuration:\n";
    std::cout << "  Threads:     " << config.num_threads << "\n";
    std::cout << "  Iterations:  " << config.iterations << "\n";
    std::cout << "  PMR Size:    " << config.pmr_size_mb << " MB\n\n";
    
    // Check CPU support
    if (!primitives::check_mwait_support()) {
        std::cerr << "Error: MONITOR/MWAIT not supported on this CPU\n";
        return 1;
    }
    
    // Initialize CXL MWAIT
    CXLMWait mwait;
    std::string device_path = "/sys/bus/cxl/devices/mem0";
    
    if (!mwait.initialize(device_path)) {
        std::cerr << "Error: Failed to initialize CXL device: " 
                  << mwait.get_last_error() << "\n";
        return 1;
    }
    
    // Map PMR
    size_t pmr_size = config.pmr_size_mb * 1024 * 1024;
    void* pmr_addr = utils::map_cxl_pmr(device_path, 0, pmr_size);
    if (!pmr_addr) {
        std::cerr << "Error: Failed to map PMR\n";
        return 1;
    }
    
    // Run benchmarks
    std::vector<std::pair<std::string, BenchmarkResult>> results;
    
    // 1. MWAIT latency benchmark
    auto mwait_result = benchmark_mwait_latency(mwait, pmr_addr, config);
    results.push_back({"MWAIT Latency", mwait_result});
    
    // 2. PMR access benchmark
    auto pmr_result = benchmark_pmr_access(pmr_addr, pmr_size, config);
    results.push_back({"PMR Access", pmr_result});
    
    // 3. Multi-threaded benchmark (if requested)
    if (config.num_threads > 1) {
        auto mt_result = benchmark_multithreaded(mwait, pmr_addr, config);
        results.push_back({"Multi-threaded MWAIT", mt_result});
    }
    
    // Print all results
    std::cout << "\n\n=== BENCHMARK RESULTS ===\n";
    for (const auto& [name, result] : results) {
        print_results(name, result);
    }
    
    // Cleanup
    utils::unmap_cxl_pmr(pmr_addr, pmr_size);
    
    std::cout << "\n✓ Benchmark completed successfully\n";
    
    return 0;
}
