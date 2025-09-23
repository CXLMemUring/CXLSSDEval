#ifndef CXL_MVVM_ADAPTER_HPP
#define CXL_MVVM_ADAPTER_HPP

#include <string>
#include <vector>

namespace cxl_mvvm {

// Launches the WASM module and checkpoints quickly to file using MVVM/WAMR.
// Returns true on success. Requires building with CXL_ENABLE_MVVM=ON.
bool mvvm_checkpoint(const std::string& wasm_path,
                     const std::vector<std::string>& args,
                     const std::string& out_file,
                     bool jit = false);

// Restores a checkpoint and resumes execution. Returns true on success.
bool mvvm_restore(const std::string& wasm_path,
                  const std::string& in_file,
                  bool jit = false);

} // namespace cxl_mvvm

#endif // CXL_MVVM_ADAPTER_HPP

