#include "../include/wasm_scheduler.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef CXL_ENABLE_MVVM
#include "../lib/MVVM/include/wamr.h"
#include "../lib/MVVM/include/wamr_export.h"
#include "../lib/MVVM/include/wamr_read_write.h"
#include "../lib/MVVM/include/wamr_exec_env.h"
#include "ylt/struct_pack.hpp"
using ylt::struct_pack::deserialize;
// Global symbols provided by MVVM
extern WAMRInstance* wamr;
extern WriteStream* writer;
extern ReadStream* reader;
#endif

namespace cxl {

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
        auto exec_envs = deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader);
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

WasmScheduler::~WasmScheduler() { shutdown(); }

int WasmScheduler::launch(const WasmTaskDesc& desc, TargetArch target) {
    std::lock_guard<std::mutex> lk(mu_);
    int id = next_id_++;
    auto task = std::make_unique<WasmTask>(desc, target);
    task->start();
    tasks_.push_back(Entry{id, std::move(task)});
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
}

} // namespace cxl
