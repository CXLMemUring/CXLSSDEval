#include "../include/cxl_ssd_common.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <mutex>

namespace cxl_ssd {

// Logger implementation
LogLevel Logger::current_level = LogLevel::INFO;
static std::mutex log_mutex;

void Logger::log(LogLevel level, const std::string& message) {
    if (level < current_level) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex);
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Format timestamp
    std::tm* tm_info = std::localtime(&time_t);
    
    // Level string
    const char* level_str = nullptr;
    switch (level) {
        case LogLevel::TRACE:   level_str = "TRACE"; break;
        case LogLevel::DEBUG:   level_str = "DEBUG"; break;
        case LogLevel::INFO:    level_str = "INFO "; break;
        case LogLevel::WARNING: level_str = "WARN "; break;
        case LogLevel::ERROR:   level_str = "ERROR"; break;
        case LogLevel::FATAL:   level_str = "FATAL"; break;
    }
    
    // Output log message
    std::cerr << "[" << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S");
    std::cerr << "." << std::setfill('0') << std::setw(3) << ms.count();
    std::cerr << "] [" << level_str << "] " << message << std::endl;
    
    if (level == LogLevel::FATAL) {
        std::abort();
    }
}

void Logger::set_level(LogLevel level) {
    current_level = level;
}

LogLevel Logger::get_level() {
    return current_level;
}

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