#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>

using namespace cxl_ssd;

// Simple PMR cache implementation using MWAIT
class PMRCache {
private:
    struct CacheLine {
        uint64_t tag;
        uint64_t data[7];  // 64 bytes total
        volatile uint64_t status;  // 0=invalid, 1=valid, 2=dirty
    };
    
    CacheLine* cache_lines;
    size_t num_lines;
    CXLMWait* mwait;
    void* pmr_base;
    
public:
    PMRCache(size_t size_mb, CXLMWait* mw, void* base) 
        : mwait(mw), pmr_base(base) {
        num_lines = (size_mb * 1024 * 1024) / sizeof(CacheLine);
        cache_lines = static_cast<CacheLine*>(base);
        
        // Initialize cache
        for (size_t i = 0; i < num_lines; i++) {
            cache_lines[i].tag = 0;
            cache_lines[i].status = 0;
        }
    }
    
    // Wait for cache line to become valid
    bool wait_for_line(size_t index, uint32_t timeout_us = 1000000) {
        if (index >= num_lines) return false;
        
        MWaitConfig config;
        config.monitor_address = (void*)&cache_lines[index].status;
        config.timeout_us = timeout_us;
        config.hint = MWaitHint::C1;
        config.granularity = MonitorGranularity::CACHE_LINE;
        
        // Check if already valid
        if (cache_lines[index].status == 1) {
            return true;
        }
        
        // Wait for status change
        MWaitStatus status = mwait->monitor_wait(config);
        return (status == MWaitStatus::SUCCESS && cache_lines[index].status == 1);
    }
    
    // Simulate cache population
    void populate_line(size_t index, uint64_t tag) {
        if (index >= num_lines) return;
        
        cache_lines[index].tag = tag;
        for (int i = 0; i < 7; i++) {
            cache_lines[index].data[i] = tag + i;
        }
        
        // Mark as valid - this will wake any waiting threads
        cache_lines[index].status = 1;
    }
    
    // Get cache statistics
    void print_stats() {
        size_t valid_lines = 0;
        size_t dirty_lines = 0;
        
        for (size_t i = 0; i < num_lines; i++) {
            if (cache_lines[i].status == 1) valid_lines++;
            if (cache_lines[i].status == 2) dirty_lines++;
        }
        
        std::cout << "Cache Statistics:\n";
        std::cout << "  Total lines:  " << num_lines << "\n";
        std::cout << "  Valid lines:  " << valid_lines << "\n";
        std::cout << "  Dirty lines:  " << dirty_lines << "\n";
        std::cout << "  Utilization:  " << (100.0 * valid_lines / num_lines) << "%\n";
    }
};

int main() {
    std::cout << "CXL SSD PMR Cache Example\n";
    std::cout << "========================\n\n";
    
    // Initialize CXL MWAIT
    CXLMWait mwait;
    std::string device_path = "/sys/bus/cxl/devices/mem0";
    
    if (!mwait.initialize(device_path)) {
        std::cerr << "Error: Failed to initialize CXL device: " 
                  << mwait.get_last_error() << "\n";
        return 1;
    }
    
    // Map 16MB of PMR for cache
    size_t cache_size = 16 * 1024 * 1024;  // 16MB
    void* pmr_addr = utils::map_cxl_pmr(device_path, 0, cache_size);
    if (!pmr_addr) {
        std::cerr << "Error: Failed to map PMR\n";
        return 1;
    }
    
    std::cout << "✓ Mapped " << (cache_size / 1024 / 1024) << "MB PMR cache\n\n";
    
    // Create PMR cache
    PMRCache cache(16, &mwait, pmr_addr);
    
    // Example 1: Producer-Consumer pattern
    std::cout << "Example 1: Producer-Consumer Cache Pattern\n";
    std::cout << "-------------------------------------------\n";
    
    const int num_entries = 10;
    std::vector<std::thread> consumers;
    std::vector<std::thread> producers;
    
    // Start consumers
    for (int i = 0; i < 3; i++) {
        consumers.emplace_back([&cache, i, num_entries]() {
            std::cout << "Consumer " << i << " started\n";
            
            for (int j = 0; j < num_entries; j++) {
                size_t index = (i * num_entries + j) % 256;
                
                if (cache.wait_for_line(index, 5000000)) {
                    std::cout << "  Consumer " << i << " got line " << index << "\n";
                } else {
                    std::cout << "  Consumer " << i << " timeout on line " << index << "\n";
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Start producers
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    for (int i = 0; i < 2; i++) {
        producers.emplace_back([&cache, i, num_entries]() {
            std::cout << "Producer " << i << " started\n";
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);
            
            for (int j = 0; j < num_entries * 2; j++) {
                size_t index = dis(gen);
                uint64_t tag = (i << 32) | j;
                
                cache.populate_line(index, tag);
                std::cout << "  Producer " << i << " populated line " << index << "\n";
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    
    // Wait for completion
    for (auto& t : consumers) t.join();
    for (auto& t : producers) t.join();
    
    std::cout << "\n";
    cache.print_stats();
    
    // Example 2: Monitoring multiple cache lines
    std::cout << "\nExample 2: Batch Cache Line Monitoring\n";
    std::cout << "--------------------------------------\n";
    
    // Reset some cache lines
    for (int i = 0; i < 5; i++) {
        static_cast<PMRCache::CacheLine*>(pmr_addr)[i].status = 0;
    }
    
    // Create batch monitor configs
    std::vector<MWaitConfig> configs;
    for (int i = 0; i < 5; i++) {
        MWaitConfig config;
        config.monitor_address = &static_cast<PMRCache::CacheLine*>(pmr_addr)[i].status;
        config.timeout_us = 3000000;
        config.hint = MWaitHint::C1;
        configs.push_back(config);
    }
    
    // Start updater thread
    std::thread updater([&cache]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        for (int i = 0; i < 5; i++) {
            cache.populate_line(i, 0x1000 + i);
            std::cout << "  Updated cache line " << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
    
    // Monitor batch
    std::cout << "Monitoring 5 cache lines...\n";
    MWaitStatus status = mwait.monitor_wait_batch(configs);
    
    if (status == MWaitStatus::SUCCESS) {
        std::cout << "✓ Batch monitor detected update\n";
    } else {
        std::cout << "✗ Batch monitor timed out\n";
    }
    
    updater.join();
    
    // Final statistics
    std::cout << "\nFinal Statistics:\n";
    auto stats = mwait.get_stats();
    std::cout << "  Total MWAIT operations:  " << stats.total_waits << "\n";
    std::cout << "  Successful wakeups:      " << stats.successful_wakes << "\n";
    std::cout << "  Timeouts:                " << stats.timeouts << "\n";
    std::cout << "  Average wait time:       " << stats.avg_wait_time.count() << " ns\n";
    
    // Cleanup
    utils::unmap_cxl_pmr(pmr_addr, cache_size);
    
    std::cout << "\n✓ PMR Cache example completed\n";
    
    return 0;
}