#ifndef CXL_WASM_SCHEDULER_HPP
#define CXL_WASM_SCHEDULER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace cxl {

enum class TargetArch { X86_64, ARM64 };

// Abstract interface for a WASM runtime binding. Concrete implementations can
// wrap Wasmtime/Wasmer/WAMR. This repository ships a stub that executes a
// native callback to simulate a wasm function for portability.
class IWasmRuntime {
public:
    virtual ~IWasmRuntime() = default;
    virtual bool load_module(const std::string& path) = 0;
    virtual bool instantiate() = 0;
    virtual bool call_export(const std::string& name, const std::vector<uint64_t>& args) = 0;
    virtual std::vector<uint8_t> snapshot() = 0;     // serialize instance state
    virtual bool restore(const std::vector<uint8_t>& state) = 0; // restore state
};

// A portable representation of a wasm task that can be checkpointed and moved
// between heterogeneous hosts.
struct WasmTaskDesc {
    std::string module_path;     // .wasm file path
    std::string entry;           // export name
    std::vector<uint64_t> args;  // raw args (e.g., pointers/values)
};

class WasmTask {
public:
    WasmTask(const WasmTaskDesc& d, TargetArch t);
    ~WasmTask();

    // Start execution in a worker thread
    void start();
    // Request cooperative checkpoint; returns serialized state
    std::vector<uint8_t> checkpoint();
    // Stop and join
    void stop();

    TargetArch arch() const { return target_; }
    const WasmTaskDesc& desc() const { return desc_; }

    // Restore from state and continue
    bool restore_and_resume(const std::vector<uint8_t>& state);

private:
    WasmTaskDesc desc_;
    TargetArch target_;
    std::unique_ptr<IWasmRuntime> rt_;
    std::thread thr_;
    std::atomic<bool> running_{false};
    std::mutex mu_;
    std::condition_variable cv_;
};

// Simple scheduler that can migrate tasks between arches by checkpointing and
// recreating them with a runtime appropriate for the destination.
class WasmScheduler {
public:
    WasmScheduler() = default;
    ~WasmScheduler();

    // Launch a task on a target architecture
    int launch(const WasmTaskDesc& desc, TargetArch target);
    // Migrate a running task to another architecture (stop->checkpoint->spawn)
    bool migrate(int task_id, TargetArch new_target);
    // Stop all tasks
    void shutdown();

private:
    struct Entry { int id; std::unique_ptr<WasmTask> task; };
    std::mutex mu_;
    std::vector<Entry> tasks_;
    int next_id_ = 1;
};

// Factory for a suitable runtime on this host
std::unique_ptr<IWasmRuntime> make_wasm_runtime(TargetArch target);

} // namespace cxl

#endif // CXL_WASM_SCHEDULER_HPP

