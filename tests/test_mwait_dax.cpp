#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstring>
#include <random>
#include <algorithm>

// Include the DAX implementation
#include "../src/cxl_mwait_dax.cpp"

using namespace cxl_dax;
using namespace std::chrono;

class DAXTester {
private:
    DAXDevice device;
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> total_ops{0};

public:
    bool init(const std::string& dax_path) {
        return device.init(dax_path);
    }

    void test_basic_operations() {
        std::cout << "\n=== Basic DAX Operations Test ===" << std::endl;

        // Test write and read
        const char* test_data = "Hello DAX World!";
        size_t data_len = strlen(test_data) + 1;

        device.write(0, test_data, data_len);

        char read_buffer[128] = {0};
        device.read(0, read_buffer, data_len);

        std::cout << "Write/Read test: "
                  << (strcmp(test_data, read_buffer) == 0 ? "PASSED" : "FAILED")
                  << std::endl;

        // Test atomic operations
        uint64_t test_value = 0x123456789ABCDEF0;
        device.store<uint64_t>(1024, test_value);
        uint64_t read_value = device.load<uint64_t>(1024);

        std::cout << "Atomic store/load test: "
                  << (test_value == read_value ? "PASSED" : "FAILED")
                  << std::endl;
    }

    void test_byte_addressable() {
        std::cout << "\n=== Byte-Addressable Test ===" << std::endl;

        // Test various small sizes
        std::vector<size_t> sizes = {1, 7, 15, 31, 63, 127, 255, 383, 511};

        for (size_t size : sizes) {
            std::vector<uint8_t> write_data(size);
            std::vector<uint8_t> read_data(size);

            // Fill with random data
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);

            for (size_t i = 0; i < size; i++) {
                write_data[i] = dis(gen);
            }

            // Write at unaligned offset
            size_t offset = 1337 + size;
            device.write(offset, write_data.data(), size);
            device.read(offset, read_data.data(), size);

            bool match = (memcmp(write_data.data(), read_data.data(), size) == 0);
            std::cout << "Size " << size << " bytes: "
                     << (match ? "PASSED" : "FAILED") << std::endl;
        }
    }

    void test_mwait_performance() {
        std::cout << "\n=== MWAIT Performance Test ===" << std::endl;

        const size_t num_iterations = 1000;
        const size_t monitor_offset = 4096;

        // Initialize monitored location
        device.store<uint32_t>(monitor_offset, 0);

        // Producer thread
        std::thread producer([this, monitor_offset, num_iterations]() {
            std::this_thread::sleep_for(milliseconds(10)); // Let consumer set up

            for (size_t i = 1; i <= num_iterations; i++) {
                std::this_thread::sleep_for(microseconds(100));
                device.store<uint32_t>(monitor_offset, i);
            }
        });

        // Consumer with MWAIT
        auto start = high_resolution_clock::now();
        size_t successful_waits = 0;

        for (size_t i = 0; i < num_iterations; i++) {
            if (device.monitor_wait(monitor_offset, i, 10000)) { // 10ms timeout
                successful_waits++;
            }
        }

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);

        producer.join();

        std::cout << "MWAIT test completed:" << std::endl;
        std::cout << "  Successful waits: " << successful_waits << "/" << num_iterations << std::endl;
        std::cout << "  Total time: " << duration.count() << " µs" << std::endl;
        std::cout << "  Avg wait time: " << duration.count() / num_iterations << " µs" << std::endl;
    }

    void throughput_test(size_t block_size, int duration_sec) {
        std::cout << "\n=== Throughput Test (Block size: " << block_size << " bytes) ===" << std::endl;

        stop_flag = false;
        total_ops = 0;

        // Worker threads
        const size_t num_threads = std::thread::hardware_concurrency();
        std::vector<std::thread> workers;

        auto worker_fn = [this, block_size]() {
            std::vector<uint8_t> buffer(block_size);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> offset_dis(0, device.get_size() - block_size);

            while (!stop_flag) {
                size_t offset = offset_dis(gen);

                // Write
                device.write(offset, buffer.data(), block_size);
                total_ops++;

                // Read
                device.read(offset, buffer.data(), block_size);
                total_ops++;
            }
        };

        auto start = high_resolution_clock::now();

        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back(worker_fn);
        }

        std::this_thread::sleep_for(seconds(duration_sec));
        stop_flag = true;

        for (auto& w : workers) {
            w.join();
        }

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);

        double ops_per_sec = (total_ops.load() * 1000.0) / duration.count();
        double bandwidth_mbps = (ops_per_sec * block_size) / (1024 * 1024);

        std::cout << "Results:" << std::endl;
        std::cout << "  Total operations: " << total_ops.load() << std::endl;
        std::cout << "  Operations/sec: " << ops_per_sec << std::endl;
        std::cout << "  Bandwidth: " << bandwidth_mbps << " MB/s" << std::endl;
    }

    void latency_test() {
        std::cout << "\n=== Latency Test ===" << std::endl;

        const size_t num_ops = 10000;
        std::vector<uint64_t> latencies;
        latencies.reserve(num_ops);

        std::vector<uint8_t> buffer(4096);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> offset_dis(0, device.get_size() - 4096);

        for (size_t i = 0; i < num_ops; i++) {
            size_t offset = offset_dis(gen);

            auto start = high_resolution_clock::now();
            device.write(offset, buffer.data(), 4096);
            device.flush(); // Ensure persistence
            auto end = high_resolution_clock::now();

            latencies.push_back(duration_cast<nanoseconds>(end - start).count());
        }

        // Calculate statistics
        std::sort(latencies.begin(), latencies.end());

        uint64_t sum = 0;
        for (auto lat : latencies) {
            sum += lat;
        }

        double avg = sum / (double)num_ops;
        double p50 = latencies[num_ops * 0.50];
        double p90 = latencies[num_ops * 0.90];
        double p99 = latencies[num_ops * 0.99];
        double p999 = latencies[num_ops * 0.999];

        std::cout << "Latency statistics (4KB writes):" << std::endl;
        std::cout << "  Average: " << avg / 1000 << " µs" << std::endl;
        std::cout << "  P50: " << p50 / 1000 << " µs" << std::endl;
        std::cout << "  P90: " << p90 / 1000 << " µs" << std::endl;
        std::cout << "  P99: " << p99 / 1000 << " µs" << std::endl;
        std::cout << "  P99.9: " << p999 / 1000 << " µs" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mem_device_path> [test_type]" << std::endl;
        std::cerr << "  mem_device_path: e.g., /dev/mem with offset 0x100000000" << std::endl;
        std::cerr << "  test_type: basic, byte, mwait, throughput, latency, all (default: all)" << std::endl;
        return 1;
    }

    std::string dax_path = argv[1];
    std::string test_type = (argc > 2) ? argv[2] : "all";

    DAXTester tester;

    if (!tester.init(dax_path)) {
        std::cerr << "Failed to initialize DAX device: " << dax_path << std::endl;
        return 1;
    }

    std::cout << "DAX device initialized: " << dax_path << std::endl;

    if (test_type == "basic" || test_type == "all") {
        tester.test_basic_operations();
    }

    if (test_type == "byte" || test_type == "all") {
        tester.test_byte_addressable();
    }

    if (test_type == "mwait" || test_type == "all") {
        tester.test_mwait_performance();
    }

    if (test_type == "throughput" || test_type == "all") {
        tester.throughput_test(4096, 5);     // 4KB blocks
        tester.throughput_test(256, 5);      // 256B blocks (sub-512)
        tester.throughput_test(64, 5);       // 64B blocks (cache line)
    }

    if (test_type == "latency" || test_type == "all") {
        tester.latency_test();
    }

    std::cout << "\nAll tests completed!" << std::endl;

    return 0;
}