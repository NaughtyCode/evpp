#include "runtime/core/log/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/backend/BackendOptions.h>
#include <quill/backend/BackendUtilities.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>

#include "runtime/config/config.h"

namespace engine {

namespace {

std::atomic<bool> g_backend_started{false};
std::atomic<bool> g_logger_active{false};
thread_local bool t_thread_name_checked = false;

quill::BackendOptions GetBackendOptions() {
	constexpr auto kSleepDuration = std::chrono::microseconds{500};
	constexpr size_t kTransitEventsSoftLimit = 16384;
	constexpr auto kSinkMinFlushInterval = std::chrono::milliseconds{100};

	quill::BackendOptions opts;
	opts.sleep_duration = kSleepDuration;
	opts.transit_events_soft_limit = kTransitEventsSoftLimit;
	opts.sink_min_flush_interval = kSinkMinFlushInterval;
	return opts;
}

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
		cfg.set_rotation_max_file_size(static_cast<size_t>(config.rotation_size_mb) * 1024 * 1024);
	}

	bool valid_frequency = false;

	if (config.rotation_frequency == "minutely") {
		if (config.rotation_interval > 0) {
			cfg.set_rotation_frequency_and_interval(
				'M', static_cast<uint32_t>(config.rotation_interval));
			valid_frequency = true;
		}
	} else if (config.rotation_frequency == "hourly") {
		if (config.rotation_interval > 0) {
			cfg.set_rotation_frequency_and_interval(
				'H', static_cast<uint32_t>(config.rotation_interval));
			valid_frequency = true;
		}
	} else if (config.rotation_frequency == "daily") {
		cfg.set_rotation_time_daily(config.rotation_time_daily);
		valid_frequency = true;
	}

	if (config.rotation_naming_scheme == "date") {
		cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
	} else if (config.rotation_naming_scheme == "date_and_time") {
		cfg.set_rotation_naming_scheme(
			quill::RotatingFileSinkConfig::RotationNamingScheme::DateAndTime);
	}

	if (config.max_backup_files >= 0) {
		cfg.set_max_backup_files(static_cast<uint32_t>(config.max_backup_files));
	}

	if (valid_frequency) {
		cfg.set_rotation_on_creation(true);
	}
}

std::string make_log_path(const LogConfig& config) {
	std::string log_dir = config.dir.empty() ? "logs" : config.dir;

	std::error_code ec;
	std::filesystem::create_directories(log_dir, ec);
	if (ec) {
		std::fprintf(stderr,
					 "log: failed to create directory [%s]: %s\n",
					 log_dir.c_str(),
					 ec.message().c_str());
	}

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
	else if (level == "info")
		logger->set_log_level(quill::LogLevel::Info);
	else if (level == "warn" || level == "warning")
		logger->set_log_level(quill::LogLevel::Warning);
	else if (level == "error")
		logger->set_log_level(quill::LogLevel::Error);
	else if (level == "fatal" || level == "critical")
		logger->set_log_level(quill::LogLevel::Critical);
	else
		logger->set_log_level(quill::LogLevel::Info);
}

void ensure_current_thread_name(const char* fallback_name) {
	if (t_thread_name_checked) {
		return;
	}
	t_thread_name_checked = true;

	try {
		if (!quill::detail::get_thread_name().empty()) {
			return;
		}
	} catch (...) {
		// Best effort only. Try to set the fallback below.
	}

	try {
		quill::detail::set_thread_name(fallback_name);
	} catch (...) {
		// Thread names are diagnostic metadata; logging must still work if
		// the OS API is unavailable.
	}
}

}  // namespace

quill::Logger* GetLogger(const std::string& name) {
	if (!g_logger_active.load(std::memory_order_acquire)) {
		return nullptr;
	}
	ensure_current_thread_name("WorkerThread");
	return quill::Frontend::get_logger(name);
}

quill::Logger* CreateLogger(const LogConfig& config) {
	ensure_current_thread_name("WorkerThread");

	if (!g_backend_started.exchange(true, std::memory_order_acq_rel)) {
		quill::Backend::start(GetBackendOptions());
	}

	auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");

	quill::RotatingFileSinkConfig file_cfg;
	apply_rotation_config(file_cfg, config);

	std::string full_path = make_log_path(config);
	auto file_sink =
		quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(full_path, file_cfg);

	auto* logger = quill::Frontend::create_or_get_logger(
		config.logger_name,
		{console_sink, file_sink},
		quill::PatternFormatterOptions{config.format_pattern});

	apply_log_level(logger, config.level);
	g_logger_active.store(true, std::memory_order_release);

	ENGINE_LOG_INFO(logger, "log file: {}", full_path);
	return logger;
}

void InitLogger(const LogConfig& config) {
	ensure_current_thread_name("MainThread");

	LogConfig root_cfg = config;
	root_cfg.logger_name = "root";
	CreateLogger(root_cfg);
}

void SetCurrentThreadName(const std::string& name) {
	if (name.empty()) {
		return;
	}

	try {
		quill::detail::set_thread_name(name.c_str());
		t_thread_name_checked = true;
	} catch (...) {
		// Thread names are optional diagnostic metadata.
		t_thread_name_checked = true;
	}
}

void ShutdownLogger() {
	g_logger_active.store(false, std::memory_order_release);
	if (g_backend_started.exchange(false, std::memory_order_acq_rel)) {
		quill::Backend::stop();
	}
}

}  // namespace engine
