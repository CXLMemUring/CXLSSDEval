#include "cxl_ssd_common.hpp"
#include "cxl_logger.hpp"

namespace cxl_ssd {

// Logger is now implemented in cxl_logger.hpp using spdlog

// Error code conversion
std::string error_to_string(ErrorCode error) {
    switch (error) {
        case ErrorCode::SUCCESS:           return "Success";
        case ErrorCode::DEVICE_NOT_FOUND:  return "Device not found";
        case ErrorCode::PERMISSION_DENIED:  return "Permission denied";
        case ErrorCode::NOT_SUPPORTED:      return "Operation not supported";
        case ErrorCode::INVALID_PARAMETER:  return "Invalid parameter";
        case ErrorCode::TIMEOUT:            return "Operation timed out";
        case ErrorCode::IO_ERROR:           return "I/O error";
        case ErrorCode::MEMORY_ERROR:       return "Memory error";
        case ErrorCode::UNKNOWN_ERROR:      return "Unknown error";
        default:                           return "Undefined error";
    }
}

} // namespace cxl_ssd