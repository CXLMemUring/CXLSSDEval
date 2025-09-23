#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include "../include/cxl_logger.hpp"
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>

namespace cxl = cxl_ssd;
using namespace cxl;

// Simple PMR cache implementation using MWAIT
class PMRCache {
public:
    struct CacheLine {
        uint64_t tag;
        uint64_t data[7];  // 64 bytes total
        volatile uint64_t status;  // 0=invalid, 1=valid, 2=dirty
    };
    
private:
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
        
        CXL_LOG_INFO("Cache Statistics:");
        CXL_LOG_INFO_FMT("  Total lines:  {}", num_lines);
        CXL_LOG_INFO_FMT("  Valid lines:  {}", valid_lines);
        CXL_LOG_INFO_FMT("  Dirty lines:  {}", dirty_lines);
        CXL_LOG_INFO_FMT("  Utilization:  {}%", (100.0 * valid_lines / num_lines));
    }
};

int main() {
    CXL_LOG_INFO("CXL SSD PMR Cache Example");
    CXL_LOG_INFO("========================\n");
    
    // Initialize CXL MWAIT
    CXLMWait mwait;
    std::string device_path = "/sys/bus/cxl/devices/mem0";
    
    if (!mwait.initialize(device_path)) {
        CXL_LOG_ERROR_FMT("Error: Failed to initialize CXL device: {}", mwait.get_last_error());
        return 1;
    }
    
    // Map 16MB of PMR for cache
    size_t cache_size = 16 * 1024 * 1024;  // 16MB
    void* pmr_addr = utils::map_cxl_pmr(device_path, 0, cache_size);
    if (!pmr_addr) {
        CXL_LOG_ERROR("Error: Failed to map PMR");
        return 1;
    }
    
    CXL_LOG_INFO_FMT("✓ Mapped {}MB PMR cache\n", (cache_size / 1024 / 1024));
    
    // Create PMR cache
    PMRCache cache(16, &mwait, pmr_addr);
    
    // Example 1: Producer-Consumer pattern
    CXL_LOG_INFO("Example 1: Producer-Consumer Cache Pattern");
    CXL_LOG_INFO("-------------------------------------------");
    
    const int num_entries = 10;
    std::vector<std::thread> consumers;
    std::vector<std::thread> producers;
    
    // Start consumers
    for (int i = 0; i < 3; i++) {
        consumers.emplace_back([&cache, i, num_entries]() {
            CXL_LOG_INFO_FMT("Consumer {} started", i);
            
            for (int j = 0; j < num_entries; j++) {
                size_t index = (i * num_entries + j) % 256;
                
                if (cache.wait_for_line(index, 5000000)) {
                    CXL_LOG_INFO_FMT("  Consumer {} got line {}", i, index);
                } else {
                    CXL_LOG_INFO_FMT("  Consumer {} timeout on line {}", i, index);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Start producers
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    for (int i = 0; i < 2; i++) {
        producers.emplace_back([&cache, i, num_entries]() {
            CXL_LOG_INFO_FMT("Producer {} started", i);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);
            
            for (int j = 0; j < num_entries * 2; j++) {
                size_t index = dis(gen);
                uint64_t tag = (static_cast<uint64_t>(i) << 32) | j;
                
                cache.populate_line(index, tag);
                CXL_LOG_INFO_FMT("  Producer {} populated line {}", i, index);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    
    // Wait for completion
    for (auto& t : consumers) t.join();
    for (auto& t : producers) t.join();
    
    CXL_LOG_INFO("");
    cache.print_stats();
    
    // Example 2: Monitoring multiple cache lines
    CXL_LOG_INFO("\nExample 2: Batch Cache Line Monitoring");
    CXL_LOG_INFO("--------------------------------------");
    
    // Reset some cache lines
    for (int i = 0; i < 5; i++) {
        static_cast<PMRCache::CacheLine*>(pmr_addr)[i].status = 0;
    }
    
    // Create batch monitor configs
    std::vector<MWaitConfig> configs;
    for (int i = 0; i < 5; i++) {
        MWaitConfig config;
        config.monitor_address = const_cast<uint64_t*>(&static_cast<PMRCache::CacheLine*>(pmr_addr)[i].status);
        config.timeout_us = 3000000;
        config.hint = MWaitHint::C1;
        configs.push_back(config);
    }
    
    // Start updater thread
    std::thread updater([&cache]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        for (int i = 0; i < 5; i++) {
            cache.populate_line(i, 0x1000 + i);
            CXL_LOG_INFO_FMT("  Updated cache line {}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
    
    // Monitor batch
    CXL_LOG_INFO("Monitoring 5 cache lines...");
    MWaitStatus status = mwait.monitor_wait_batch(configs);
    
    if (status == MWaitStatus::SUCCESS) {
        CXL_LOG_INFO("✓ Batch monitor detected update");
    } else {
        CXL_LOG_INFO("✗ Batch monitor timed out");
    }
    
    updater.join();
    
    // Final statistics
    CXL_LOG_INFO("\nFinal Statistics:");
    auto stats = mwait.get_stats();
    CXL_LOG_INFO_FMT("  Total MWAIT operations:  {}", stats.total_waits);
    CXL_LOG_INFO_FMT("  Successful wakeups:      {}", stats.successful_wakes);
    CXL_LOG_INFO_FMT("  Timeouts:                {}", stats.timeouts);
    CXL_LOG_INFO_FMT("  Average wait time:       {} ns", stats.avg_wait_time.count());
    
    // Cleanup
    utils::unmap_cxl_pmr(pmr_addr, cache_size);
    
    CXL_LOG_INFO("\n✓ PMR Cache example completed");
    
    return 0;
}
