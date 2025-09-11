#ifndef CXL_LOGGER_HPP
#define CXL_LOGGER_HPP

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <string>

namespace cxl_ssd {
    // Logger wrapper for backwards compatibility
    enum class LogLevel {
        TRACE = SPDLOG_LEVEL_TRACE,
        DEBUG_ = SPDLOG_LEVEL_DEBUG,
        INFO = SPDLOG_LEVEL_INFO,
        WARNING = SPDLOG_LEVEL_WARN,
        ERROR = SPDLOG_LEVEL_ERROR,
        FATAL = SPDLOG_LEVEL_CRITICAL
    };

    class Logger {
    private:
        static std::shared_ptr<spdlog::logger> logger_;
        static LogLevel current_level;

    public:
        // Initialize logger (call once at startup)
        static void init(const std::string &name = "cxl_ssd") {
            if (!logger_) {
                logger_ = spdlog::stdout_color_mt(name);
                logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
                logger_->set_level(spdlog::level::info);
            }
        }

        // Initialize with file output
        static void init_with_file(const std::string &name, const std::string &filename) {
            if (!logger_) {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);

                logger_ = std::make_shared<spdlog::logger>(name,
                                                           spdlog::sinks_init_list{console_sink, file_sink});
                logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
                logger_->set_level(spdlog::level::info);
            }
        }

        // Get logger instance
        static std::shared_ptr<spdlog::logger> get() {
            if (!logger_) {
                init();
            }
            return logger_;
        }

        // Compatibility methods
        static void log(LogLevel level, const std::string &message) {
            if (!logger_) init();

            switch (level) {
                case LogLevel::TRACE:
                    logger_->trace(message);
                    break;
                case LogLevel::DEBUG_:
                    logger_->debug(message);
                    break;
                case LogLevel::INFO:
                    logger_->info(message);
                    break;
                case LogLevel::WARNING:
                    logger_->warn(message);
                    break;
                case LogLevel::ERROR:
                    logger_->error(message);
                    break;
                case LogLevel::FATAL:
                    logger_->critical(message);
                    std::abort();
            }
        }

        static void set_level(LogLevel level) {
            if (!logger_) init();
            current_level = level;
            logger_->set_level(static_cast<spdlog::level::level_enum>(level));
        }

        static LogLevel get_level() {
            return current_level;
        }
    };

    // Initialize static members
    inline std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
    inline LogLevel Logger::current_level = LogLevel::INFO;
} // namespace cxl_ssd

// Convenience macros for logging
#define CXL_LOG_TRACE(msg) cxl_ssd::Logger::get()->trace(msg)
#define CXL_LOG_DEBUG(msg) cxl_ssd::Logger::get()->debug(msg)
#define CXL_LOG_INFO(msg) cxl_ssd::Logger::get()->info(msg)
#define CXL_LOG_WARN(msg) cxl_ssd::Logger::get()->warn(msg)
#define CXL_LOG_ERROR(msg) cxl_ssd::Logger::get()->error(msg)
#define CXL_LOG_CRITICAL(msg) cxl_ssd::Logger::get()->critical(msg)

// Formatted logging macros
#define CXL_LOG_TRACE_FMT(...) cxl_ssd::Logger::get()->trace(__VA_ARGS__)
#define CXL_LOG_DEBUG_FMT(...) cxl_ssd::Logger::get()->debug(__VA_ARGS__)
#define CXL_LOG_INFO_FMT(...) cxl_ssd::Logger::get()->info(__VA_ARGS__)
#define CXL_LOG_WARN_FMT(...) cxl_ssd::Logger::get()->warn(__VA_ARGS__)
#define CXL_LOG_ERROR_FMT(...) cxl_ssd::Logger::get()->error(__VA_ARGS__)
#define CXL_LOG_CRITICAL_FMT(...) cxl_ssd::Logger::get()->critical(__VA_ARGS__)

#endif // CXL_LOGGER_HPP
