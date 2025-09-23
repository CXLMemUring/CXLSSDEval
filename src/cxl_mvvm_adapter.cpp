#include "../include/cxl_mvvm_adapter.hpp"

#ifdef CXL_ENABLE_MVVM

#include "../lib/MVVM/include/wamr.h"
#include "../lib/MVVM/include/wamr_export.h"
#include "../lib/MVVM/include/wamr_read_write.h"
#include "ylt/struct_pack.hpp"
#include <memory>

extern WAMRInstance* wamr;
extern WriteStream* writer;
extern ReadStream* reader;
extern size_t snapshot_threshold;
extern int stop_func_threshold;
extern bool is_debug;
extern int stop_func_index;

namespace cxl_mvvm {

static std::vector<const char*> to_cstrv(const std::vector<std::string>& v) {
    std::vector<const char*> r; r.reserve(v.size());
    for (auto& s : v) r.push_back(s.c_str());
    return r;
}

bool mvvm_checkpoint(const std::string& wasm_path,
                     const std::vector<std::string>& args,
                     const std::string& out_file,
                     bool jit) {
    // Configure quick snapshot at first opportunity
    snapshot_threshold = 1; // minimal steps
    stop_func_threshold = 0;
    is_debug = false;
    stop_func_index = 0;

    // Prepare WAMR
    std::vector<std::string> argv = args;
    if (argv.empty() || argv.front() != wasm_path)
        argv.insert(argv.begin(), wasm_path);

    std::vector<std::string> empty;
    writer = new FwriteStream(out_file.c_str());

    wamr = new WAMRInstance(wasm_path.c_str(), jit);
    wamr->set_wasi_args(empty, empty, empty, argv, empty, empty);
    wamr->instantiate();
    (void)wamr->get_int3_addr();
    (void)wamr->replace_int3_with_nop();
    (void)wamr->replace_mfence_with_nop();

    // Call wasi _start so MVVM hooks perform checkpoint and emit to writer
    int rc = wamr->invoke_main();
    (void)rc;
    return true;
}

bool mvvm_restore(const std::string& wasm_path,
                  const std::string& in_file,
                  bool jit) {
    reader = new FreadStream(in_file.c_str());

    wamr = new WAMRInstance(wasm_path.c_str(), jit);
    wamr->instantiate();
    (void)wamr->get_int3_addr();
    (void)wamr->replace_int3_with_nop();

    auto exec_envs = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader);
    if (!exec_envs.has_value()) return false;
    auto v = std::move(exec_envs.value());
    wamr->recover(&v);
    return true;
}

} // namespace cxl_mvvm

#else

namespace cxl_mvvm {
bool mvvm_checkpoint(const std::string&, const std::vector<std::string>&, const std::string&, bool) { return false; }
bool mvvm_restore(const std::string&, const std::string&, bool) { return false; }
}

#endif

