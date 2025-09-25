#include "../include/wasm_scheduler.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>

#ifdef CXL_ENABLE_MVVM
#include "../lib/MVVM/include/wamr.h"
#include "../lib/MVVM/include/wamr_export.h"
#include "../lib/MVVM/include/wamr_read_write.h"
#include "../lib/MVVM/include/wamr_exec_env.h"
#include "ylt/struct_pack.hpp"
// Global symbols provided by MVVM
extern WAMRInstance* wamr;
extern WriteStream* writer;
extern ReadStream* reader;
#endif

namespace cxl {

// System monitoring utilities including thermal monitoring
struct SystemMetrics {
    double page_cache_mb;
    double cpu_temp_celsius;
    double memory_pressure;
    uint64_t timestamp;
};

class SystemMonitor {
public:
    SystemMonitor(double cache_threshold_mb = 8192.0, double temp_threshold_c = 80.0)
        : cache_threshold_(cache_threshold_mb), temp_threshold_(temp_threshold_c) {
        // Get thresholds from environment if available
        const char* cache_env = getenv("MIGRATION_CACHE_THRESHOLD_MB");
        if (cache_env) cache_threshold_ = std::stod(cache_env);

        const char* temp_env = getenv("MIGRATION_TEMP_THRESHOLD_C");
        if (temp_env) temp_threshold_ = std::stod(temp_env);
    }

    SystemMetrics get_metrics() {
        SystemMetrics m = {};
        m.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        // Get page cache from /proc/meminfo
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        uint64_t cached_kb = 0, buffers_kb = 0;
        while (std::getline(meminfo, line)) {
            if (line.find("Cached:") == 0) {
                sscanf(line.c_str(), "Cached: %lu kB", &cached_kb);
            } else if (line.find("Buffers:") == 0) {
                sscanf(line.c_str(), "Buffers: %lu kB", &buffers_kb);
            }
        }
        m.page_cache_mb = (cached_kb + buffers_kb) / 1024.0;

        // Get CPU temperature from thermal zones
        double max_temp = 0.0;
        for (int i = 0; i < 10; i++) {
            std::string thermal_path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
            std::ifstream temp_file(thermal_path);
            if (temp_file.is_open()) {
                int temp_millidegree;
                temp_file >> temp_millidegree;
                double temp_c = temp_millidegree / 1000.0;
                if (temp_c > max_temp) max_temp = temp_c;
            }
        }
        m.cpu_temp_celsius = max_temp;

        // Calculate memory pressure
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            double total_mb = (info.totalram * info.mem_unit) / (1024.0 * 1024.0);
            double free_mb = (info.freeram * info.mem_unit) / (1024.0 * 1024.0);
            m.memory_pressure = 1.0 - (free_mb / total_mb);
        }

        return m;
    }

    bool should_migrate(const SystemMetrics& m) {
        return (m.page_cache_mb > cache_threshold_) ||
               (m.cpu_temp_celsius > temp_threshold_) ||
               (m.memory_pressure > 0.85);
    }

private:
    double cache_threshold_;
    double temp_threshold_;
};

// Migration coordinator for cross-architecture transfer
class MigrationCoordinator {
public:
    MigrationCoordinator(uint16_t listen_port = 9876) : port_(listen_port) {
        const char* port_env = getenv("MIGRATION_PORT");
        if (port_env) port_ = std::stoi(port_env);
    }

    // Start listening for incoming migrations (ARM side)
    void start_receiver() {
        receiver_thread_ = std::thread([this]() {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                perror("[Migration] Failed to create socket");
                return;
            }

            int opt = 1;
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                perror("[Migration] Failed to set socket options");
                close(sock);
                return;
            }

            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port_);

            if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                perror("[Migration] Failed to bind socket");
                close(sock);
                return;
            }

            if (listen(sock, 5) < 0) {
                perror("[Migration] Failed to listen on socket");
                close(sock);
                return;
            }

            printf("[Migration] Receiver listening on port %d\n", port_);

            while (running_.load()) {
                struct sockaddr_in client_addr = {};
                socklen_t client_len = sizeof(client_addr);
                int client = accept(sock, (struct sockaddr*)&client_addr, &client_len);
                if (client < 0) {
                    if (running_.load()) {
                        perror("[Migration] Failed to accept connection");
                    }
                    continue;
                }

                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                printf("[Migration] Incoming migration from %s\n", client_ip);

                // Receive migration data
                uint32_t data_size;
                ssize_t received = recv(client, &data_size, sizeof(data_size), MSG_WAITALL);
                if (received != sizeof(data_size)) {
                    printf("[Migration] Failed to receive data size\n");
                    close(client);
                    continue;
                }

                std::vector<uint8_t> state(data_size);
                received = recv(client, state.data(), data_size, MSG_WAITALL);
                if (received != data_size) {
                    printf("[Migration] Failed to receive state data\n");
                    close(client);
                    continue;
                }

                printf("[Migration] Received %u bytes of state data\n", data_size);

                // Store for retrieval
                {
                    std::lock_guard<std::mutex> lk(state_mutex_);
                    pending_state_ = std::move(state);
                    state_ready_ = true;
                }
                state_cv_.notify_one();

                close(client);
            }
            close(sock);
        });
    }

    // Send state to remote ARM host (x86 side)
    bool send_state(const std::string& remote_host, const std::vector<uint8_t>& state) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("[Migration] Failed to create socket");
            return false;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);

        if (inet_pton(AF_INET, remote_host.c_str(), &addr.sin_addr) <= 0) {
            printf("[Migration] Invalid address: %s\n", remote_host.c_str());
            close(sock);
            return false;
        }

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("[Migration] Failed to connect");
            close(sock);
            return false;
        }

        uint32_t data_size = state.size();
        if (send(sock, &data_size, sizeof(data_size), 0) != sizeof(data_size)) {
            perror("[Migration] Failed to send data size");
            close(sock);
            return false;
        }

        if (send(sock, state.data(), state.size(), 0) != state.size()) {
            perror("[Migration] Failed to send state data");
            close(sock);
            return false;
        }

        printf("[Migration] Sent %u bytes to %s:%d\n", data_size, remote_host.c_str(), port_);
        close(sock);
        return true;
    }

    // Wait for incoming state (ARM side)
    std::vector<uint8_t> wait_for_state(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) {
        std::unique_lock<std::mutex> lk(state_mutex_);
        if (state_cv_.wait_for(lk, timeout, [this]{ return state_ready_; })) {
            state_ready_ = false;
            return std::move(pending_state_);
        }
        return {};
    }

    void stop() {
        running_.store(false);
        // Connect to self to unblock accept()
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            connect(sock, (struct sockaddr*)&addr, sizeof(addr));
            close(sock);
        }
        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }
    }

private:
    uint16_t port_;
    std::atomic<bool> running_{true};
    std::thread receiver_thread_;
    std::mutex state_mutex_;
    std::condition_variable state_cv_;
    bool state_ready_ = false;
    std::vector<uint8_t> pending_state_;
};

// A stub runtime that simulates a wasm module by invoking a native callback
// stored in the module path (for demo). Replace with a real Wasm engine
// (e.g., Wasmtime) by implementing IWasmRuntime.
class StubRuntime : public IWasmRuntime {
public:
    bool load_module(const std::string& path) override {
        module_path_ = path; return true;
    }
    bool instantiate() override { progress_ = 0; return true; }
    bool call_export(const std::string& name, const std::vector<uint64_t>& args) override {
        (void)name; (void)args; // simulate long-running work
        // Increment progress in a loop to emulate compute; cooperative
        for (int i = 0; i < 100000; ++i) {
            progress_++;
            if ((progress_ % 4096) == 0) std::this_thread::yield();
        }
        return true;
    }
    std::vector<uint8_t> snapshot() override {
        std::vector<uint8_t> s(sizeof(progress_));
        std::memcpy(s.data(), &progress_, sizeof(progress_));
        return s;
    }
    bool restore(const std::vector<uint8_t>& s) override {
        if (s.size() != sizeof(progress_)) return false;
        std::memcpy(&progress_, s.data(), sizeof(progress_));
        return true;
    }
private:
    std::string module_path_;
    uint64_t progress_ = 0;
};

#ifdef CXL_ENABLE_MVVM
// MVVM-backed runtime that runs WAMR and uses MVVM lightweight checkpoints.
class MVVMRuntime : public IWasmRuntime {
public:
    MVVMRuntime(bool jit) : jit_(jit) {}
    ~MVVMRuntime() override {
        // Ensure globals are not left dangling
        if (writer) { delete writer; writer = nullptr; }
        if (reader) { delete reader; reader = nullptr; }
        // WAMRInstance destructor will clean up WAMR runtime
        instance_.reset();
        if (wamr == instance_raw_) {
            wamr = nullptr;
        }
    }

    bool load_module(const std::string& path) override {
        module_path_ = path;
        instance_ = std::make_unique<WAMRInstance>(module_path_.c_str(), jit_);
        instance_raw_ = instance_.get();
        // Point global to this instance so MVVM internals act on it
        wamr = instance_raw_;
        return true;
    }

    bool instantiate() override {
        if (!instance_) return false;
        std::vector<std::string> empty;
        // Set up minimal WASI args so _start runs with path as argv[0]
        std::vector<std::string> argv{module_path_};
        instance_->set_wasi_args(empty, empty, empty, argv, empty, empty);
        instance_->instantiate();
        (void)instance_->get_int3_addr();
        (void)instance_->replace_int3_with_nop();
        (void)instance_->replace_mfence_with_nop();
        return true;
    }

    bool call_export(const std::string& /*name*/, const std::vector<uint64_t>& /*args*/) override {
        if (!instance_) return false;
        // For now, drive _start/main
        (void)instance_->invoke_main();
        return true;
    }

    std::vector<uint8_t> snapshot() override {
        std::vector<uint8_t> state;
        if (!instance_) return state;
        // Use a temporary file to leverage page cache and MVVM writer API
        auto tmp = std::filesystem::temp_directory_path() / "mvvm_ckpt.bin";
        // Ensure any existing writer is closed to avoid interference
        if (writer) { delete writer; writer = nullptr; }
        writer = new FwriteStream(tmp.c_str());
        // Trigger lightweight checkpoint of current exec env
        serialize_to_file(instance_->exec_env);
        // Finalize writer to flush contents
        delete writer; writer = nullptr;
        // Load file into memory
        std::ifstream ifs(tmp, std::ios::binary);
        state.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        std::error_code ec; std::filesystem::remove(tmp, ec);
        return state;
    }

    bool restore(const std::vector<uint8_t>& state) override {
        if (!instance_) return false;
        // Feed the state bytes via a temporary ReadStream
        auto tmp = std::filesystem::temp_directory_path() / "mvvm_restore.bin";
        {
            std::ofstream ofs(tmp, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(state.data()), static_cast<std::streamsize>(state.size()));
        }
        if (reader) { delete reader; reader = nullptr; }
        reader = new FreadStream(tmp.c_str());
        // Read exec env vector and recover into current instance
        auto exec_envs = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader);
        if (!exec_envs.has_value()) {
            delete reader; reader = nullptr; std::error_code ec; std::filesystem::remove(tmp, ec); return false;
        }
        auto v = std::move(exec_envs.value());
        instance_->recover(&v);
        delete reader; reader = nullptr; std::error_code ec; std::filesystem::remove(tmp, ec);
        return true;
    }

private:
    bool jit_ = false;
    std::string module_path_;
    std::unique_ptr<WAMRInstance> instance_;
    WAMRInstance* instance_raw_ = nullptr;
};
#endif // CXL_ENABLE_MVVM

std::unique_ptr<IWasmRuntime> make_wasm_runtime(TargetArch /*target*/) {
#ifdef CXL_ENABLE_MVVM
    // Prefer MVVM runtime when enabled
    return std::make_unique<MVVMRuntime>(/*jit=*/false);
#else
    // In a real deployment, select an engine build suitable for the host arch.
    return std::make_unique<StubRuntime>();
#endif
}

WasmTask::WasmTask(const WasmTaskDesc& d, TargetArch t) : desc_(d), target_(t) {
    rt_ = make_wasm_runtime(t);
    rt_->load_module(desc_.module_path);
    rt_->instantiate();
}

WasmTask::~WasmTask() { stop(); }

void WasmTask::start() {
    if (running_.exchange(true)) return;
    thr_ = std::thread([this](){
        rt_->call_export(desc_.entry, desc_.args);
        running_.store(false);
        std::unique_lock<std::mutex> ul(mu_);
        cv_.notify_all();
    });
}

std::vector<uint8_t> WasmTask::checkpoint() {
    // Cooperative checkpoint of current state
    return rt_->snapshot();
}

bool WasmTask::restore_and_resume(const std::vector<uint8_t>& state) {
    if (!rt_->restore(state)) return false;
    start();
    return true;
}

void WasmTask::stop() {
    if (!running_.load()) {
        if (thr_.joinable()) thr_.join();
        return;
    }
    // Cooperative stop: just wait for it to finish (demo). Real engines support interrupts.
    std::unique_lock<std::mutex> ul(mu_);
    cv_.wait_for(ul, std::chrono::milliseconds(10));
    if (thr_.joinable()) thr_.join();
    running_.store(false);
}

WasmScheduler::WasmScheduler()
    : monitor_(std::make_unique<SystemMonitor>()),
      coordinator_(std::make_unique<MigrationCoordinator>()) {

    printf("[WasmScheduler] Initializing live migration framework\n");
    printf("[WasmScheduler] Architecture: %s\n",
#ifdef __x86_64__
           "x86_64"
#elif defined(__aarch64__)
           "ARM64"
#else
           "Unknown"
#endif
    );

    // Start monitoring thread
    monitor_thread_ = std::thread([this]() {
        printf("[Monitor] Starting system monitoring thread\n");
        while (monitoring_.load()) {
            auto metrics = monitor_->get_metrics();
            printf("[Monitor] Cache: %.1f MB, Temp: %.1f°C, Memory: %.1f%%\n",
                   metrics.page_cache_mb, metrics.cpu_temp_celsius,
                   metrics.memory_pressure * 100);

            if (monitor_->should_migrate(metrics)) {
                trigger_migration(metrics);
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        printf("[Monitor] System monitoring thread stopped\n");
    });

#ifdef __aarch64__
    // ARM target: start receiver for incoming migrations
    printf("[WasmScheduler] Starting migration receiver (ARM target)\n");
    coordinator_->start_receiver();
#endif
}

WasmScheduler::~WasmScheduler() {
    monitoring_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    coordinator_->stop();
    shutdown();
}

int WasmScheduler::launch(const WasmTaskDesc& desc, TargetArch target) {
    std::lock_guard<std::mutex> lk(mu_);
    int id = next_id_++;
    auto task = std::make_unique<WasmTask>(desc, target);
    task->start();
    tasks_.push_back(Entry{id, std::move(task)});
    printf("[WasmScheduler] Launched task %d on %s\n", id,
           target == TargetArch::X86_64 ? "x86_64" : "ARM64");
    return id;
}

bool WasmScheduler::migrate(int task_id, TargetArch new_target) {
    std::unique_ptr<WasmTask> old_task;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->id == task_id) {
                // Snapshot state
                auto state = it->task->checkpoint();
                it->task->stop();
                old_task = std::move(it->task);
                // Create new task on new arch
                auto t = std::make_unique<WasmTask>(old_task->desc(), new_target);
                if (!t->restore_and_resume(state)) return false;
                it->task = std::move(t);
                printf("[WasmScheduler] Migrated task %d to %s\n", task_id,
                       new_target == TargetArch::X86_64 ? "x86_64" : "ARM64");
                return true;
            }
        }
    }
    return false;
}

void WasmScheduler::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : tasks_) {
        if (e.task) e.task->stop();
    }
    tasks_.clear();
    printf("[WasmScheduler] Shutdown complete\n");
}

void WasmScheduler::trigger_migration(const SystemMetrics& metrics) {
    std::lock_guard<std::mutex> lk(mu_);

    // Find the most resource-intensive task
    if (tasks_.empty()) return;

    auto& entry = tasks_.front(); // For demo, migrate first task

    printf("[Migration Trigger] Threshold exceeded!\n");
    printf("  - Page Cache: %.1f MB (threshold: check env MIGRATION_CACHE_THRESHOLD_MB)\n",
           metrics.page_cache_mb);
    printf("  - CPU Temperature: %.1f°C (threshold: check env MIGRATION_TEMP_THRESHOLD_C)\n",
           metrics.cpu_temp_celsius);
    printf("  - Memory Pressure: %.1f%%\n", metrics.memory_pressure * 100);

#ifdef __x86_64__
    // x86 source: serialize and send to ARM target
    printf("[Migration] Checkpointing task %d for migration...\n", entry.id);
    auto state = entry.task->checkpoint();
    printf("[Migration] Checkpoint size: %zu bytes\n", state.size());

    entry.task->stop();

    // Get ARM target from environment or config
    const char* arm_host = getenv("ARM_MIGRATION_HOST");
    if (!arm_host) {
        arm_host = "192.168.1.100"; // Default ARM host
        printf("[Migration] Using default ARM host: %s (set ARM_MIGRATION_HOST to override)\n", arm_host);
    }

    printf("[Migration] Sending state to ARM host %s...\n", arm_host);
    if (coordinator_->send_state(arm_host, state)) {
        printf("[Migration] Successfully sent state to ARM host %s\n", arm_host);
        // Remove task from local scheduler
        tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                     [&](const Entry& e) { return e.id == entry.id; }), tasks_.end());
        printf("[Migration] Task %d removed from x86 scheduler\n", entry.id);
    } else {
        printf("[Migration] Failed to send state, resuming locally\n");
        entry.task->restore_and_resume(state);
    }
#elif defined(__aarch64__)
    printf("[Migration] ARM platform detected - waiting for incoming migrations\n");
#else
    printf("[Migration] Unsupported platform for migration\n");
#endif
}

bool WasmScheduler::receive_migration(const WasmTaskDesc& desc) {
#ifdef __aarch64__
    printf("[WasmScheduler] Waiting for incoming migration...\n");
    // ARM target: wait for incoming state and resume
    auto state = coordinator_->wait_for_state();
    if (!state.empty()) {
        printf("[WasmScheduler] Received migration state (%zu bytes)\n", state.size());
        auto task = std::make_unique<WasmTask>(desc, TargetArch::ARM64);
        if (task->restore_and_resume(state)) {
            std::lock_guard<std::mutex> lk(mu_);
            int id = next_id_++;
            tasks_.push_back(Entry{id, std::move(task)});
            printf("[WasmScheduler] Successfully restored and resumed task %d on ARM\n", id);
            return true;
        } else {
            printf("[WasmScheduler] Failed to restore task from migration state\n");
        }
    } else {
        printf("[WasmScheduler] No migration state received within timeout\n");
    }
#else
    (void)desc;
    printf("[WasmScheduler] receive_migration() only supported on ARM platforms\n");
#endif
    return false;
}

} // namespace cxl