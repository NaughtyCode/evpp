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

void apply_rotation_config(quill::RotatingFileSinkConfig& cfg, const LogConfig& config) {
    if (config.rotation_size_mb > 0) {
        cfg.set_rotation_max_file_size(
            static_cast<size_t>(config.rotation_size_mb) * 1024 * 1024);
    }

    if (config.rotation_frequency == "minutely") {
        cfg.set_rotation_frequency_and_interval(
            'M', static_cast<uint32_t>(config.rotation_interval));
    } else if (config.rotation_frequency == "hourly") {
        cfg.set_rotation_frequency_and_interval(
            'H', static_cast<uint32_t>(config.rotation_interval));
    } else if (config.rotation_frequency == "daily") {
        cfg.set_rotation_time_daily(config.rotation_time_daily);
    }

    if (config.rotation_naming_scheme == "date") {
        cfg.set_rotation_naming_scheme(
            quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
    } else if (config.rotation_naming_scheme == "date_and_time") {
        cfg.set_rotation_naming_scheme(
            quill::RotatingFileSinkConfig::RotationNamingScheme::DateAndTime);
    }

    cfg.set_max_backup_files(static_cast<uint32_t>(config.max_backup_files));

    if (!config.rotation_frequency.empty()) {
        cfg.set_rotation_on_creation(true);
    }
}

std::string make_log_path(const LogConfig& config) {
    std::string log_dir = config.dir.empty() ? "logs" : config.dir;

    std::string prefix;
    if (!config.log_filename.empty()) {
        prefix = config.log_filename;
    } else if (!config.logger_name.empty() && config.logger_name != "root") {
        prefix = config.logger_name;
    } else {
        prefix = "engine";
    }

    if (config.rotation_frequency.empty()) {
        return log_dir + "/" + prefix + "_" + now_timestamp() + ".log";
    }
    return log_dir + "/" + prefix + ".log";
}

void apply_log_level(quill::Logger* logger, const std::string& level) {
    if (level == "trace")
        logger->set_log_level(quill::LogLevel::TraceL1);
    else if (level == "debug")
        logger->set_log_level(quill::LogLevel::Debug);
    else if (level == "warn" || level == "warning")
        logger->set_log_level(quill::LogLevel::Warning);
    else if (level == "error")
        logger->set_log_level(quill::LogLevel::Error);
    else if (level == "fatal" || level == "critical")
        logger->set_log_level(quill::LogLevel::Critical);
    else
        logger->set_log_level(quill::LogLevel::Info);
}

} // namespace

quill::Logger* GetLogger(const std::string& name) {
    return quill::Frontend::get_logger(name);
}

void InitLogger(const LogConfig& config) {
    quill::Backend::start(GetBackendOptions());

    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");

    quill::RotatingFileSinkConfig file_cfg;
    apply_rotation_config(file_cfg, config);

    std::string full_path = make_log_path(config);
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        full_path, file_cfg);

    auto* logger = quill::Frontend::create_or_get_logger(
        "root", {console_sink, file_sink},
        quill::PatternFormatterOptions{config.format_pattern});

    apply_log_level(logger, config.level);

    LOG_INFO(logger, "log file: {}", full_path);
}

quill::Logger* CreateLogger(const LogConfig& config) {
    quill::Backend::start(GetBackendOptions());

    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");

    quill::RotatingFileSinkConfig file_cfg;
    apply_rotation_config(file_cfg, config);

    std::string full_path = make_log_path(config);
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        full_path, file_cfg);

    auto* logger = quill::Frontend::create_or_get_logger(
        config.logger_name, {console_sink, file_sink},
        quill::PatternFormatterOptions{config.format_pattern});

    apply_log_level(logger, config.level);

    LOG_INFO(logger, "log file: {}", full_path);
    return logger;
}

void ShutdownLogger() {
    quill::Backend::stop();
}

} // namespace engine
