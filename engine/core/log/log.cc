#include "engine/core/log/log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/LogMacros.h>

#include "engine/config/config.h"
#include "engine/core/log/log_config.h"

namespace engine {

namespace {

std::string now_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

} // namespace

quill::Logger* GetLogger(const std::string& name) {
    return quill::Frontend::get_logger(name);
}

void InitLogger(const LogConfig& config) {
    std::string log_path = config.dir.empty() ? "logs" : config.dir;

    // Start backend thread (independent of evpp event loops)
    quill::Backend::start(GetBackendOptions());

    // Create sinks — always log to console + rotating file
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
    quill::RotatingFileSinkConfig file_cfg;
    file_cfg.set_rotation_max_file_size(
        static_cast<size_t>(config.rotation_size_mb) * 1024 * 1024);
    file_cfg.set_max_backup_files(config.max_backup_files);
    std::string full_path = log_path + "/engine_" + now_timestamp() + ".log";
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        full_path, file_cfg);

    // Create root logger with both sinks
    quill::Frontend::create_or_get_logger(
        "root", {console_sink, file_sink},
        quill::PatternFormatterOptions{config.format_pattern});

    // Apply log level filter from config
    {
        auto* root_logger = quill::Frontend::get_logger("root");
        if (config.level == "trace")
            root_logger->set_log_level(quill::LogLevel::TraceL1);
        else if (config.level == "debug")
            root_logger->set_log_level(quill::LogLevel::Debug);
        else if (config.level == "warn" || config.level == "warning")
            root_logger->set_log_level(quill::LogLevel::Warning);
        else if (config.level == "error")
            root_logger->set_log_level(quill::LogLevel::Error);
        else if (config.level == "fatal" || config.level == "critical")
            root_logger->set_log_level(quill::LogLevel::Critical);
        else
            root_logger->set_log_level(quill::LogLevel::Info);
    }

    LOG_INFO(quill::Frontend::get_logger("root"), "log file: {}", full_path);
}

void ShutdownLogger() {
    // Block until all SPSC queues are drained and written
    quill::Backend::stop();
}

} // namespace engine
