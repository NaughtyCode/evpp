#include "runtime/config/config_validator.h"

#include <cstdio>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

namespace engine {

namespace {

const std::unordered_set<std::string> kValidSandboxLevels = {
	"strict", "server", "full"
};

const std::unordered_set<std::string> kValidLogLevels = {
	"trace", "debug", "info", "warn", "error", "critical"
};

void MergeResult(ConfigValidator::Result& dest, const ConfigValidator::Result& src) {
	if (!src.valid) {
		dest.valid = false;
		if (!dest.errors.empty() && !src.errors.empty()) dest.errors += "; ";
		dest.errors += src.errors;
	}
	if (!src.warnings.empty()) {
		if (!dest.warnings.empty()) dest.warnings += "; ";
		dest.warnings += src.warnings;
	}
}

}  // namespace

// ── RuntimeConfig validation ──────────────────────────────────────────────

ConfigValidator::Result ConfigValidator::Validate(const RuntimeConfig& config) {
	Result r;

	CheckRange(r, config.frame.target_fps, 0, 1000, "frame.target_fps");
	CheckRange(r, config.frame.interval_ms, 1, 10000, "frame.interval_ms");
	CheckRange(r, config.frame.slow_threshold_multiplier, 1, 100,
			   "frame.slow_threshold_multiplier");
	CheckNotEmpty(r, config.resource_dir, "resource_dir");
	CheckNotEmpty(r, config.scripts_dir, "scripts_dir");
	CheckEnum(r, config.sandbox_level, kValidSandboxLevels, "sandbox_level");

	if (config.log.rotation_size_mb < 1 || config.log.rotation_size_mb > 10240) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "log.rotation_size_mb must be in [1, 10240], got " +
					std::to_string(config.log.rotation_size_mb);
	}
	if (config.log.max_backup_files < 0 || config.log.max_backup_files > 1000) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "log.max_backup_files must be in [0, 1000]";
	}
	CheckEnum(r, config.log.level, kValidLogLevels, "log.level");

	// Cross-field: target_fps and interval_ms should be consistent
	if (config.frame.target_fps > 0 && config.frame.interval_ms > 0) {
		int expected_ms = 1000 / config.frame.target_fps;
		if (std::abs(config.frame.interval_ms - expected_ms) > 1) {
			CheckWarning(r, true,
				"frame.target_fps=" + std::to_string(config.frame.target_fps) +
				" but interval_ms=" + std::to_string(config.frame.interval_ms) +
				" (expected ~" + std::to_string(expected_ms) + ")");
		}
	}

	if (!r.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "[config] validation failed: {}", r.errors);
	}
	if (!r.warnings.empty()) {
		if (auto* l = GetLogger()) ENGINE_LOG_WARN(l, "[config] validation warning: {}", r.warnings);
	}

	return r;
}

// ── ServerConfig validation ───────────────────────────────────────────────

ConfigValidator::Result ConfigValidator::ValidateServer(const ServerConfig& config) {
	Result r;

	// admin_port: 0 (disabled) or [1, 65535]
	if (config.admin_port != 0) {
		CheckRange(r, config.admin_port, 1, 65535, "admin_port");
	}

	// http
	CheckRange(r, static_cast<int64_t>(config.http.timeout_sec), 1, 300,
			   "http.timeout_sec");

	// msgpack
	CheckRange(r, config.msgpack.max_nesting_depth, 1, 256,
			   "msgpack.max_nesting_depth");
	if (config.msgpack.max_payload_size == 0) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "msgpack.max_payload_size must be > 0 (0 would reject all messages)";
	}
	if (config.msgpack.max_payload_size > 100 * 1024 * 1024) {
		CheckWarning(r, true,
			"msgpack.max_payload_size=" + std::to_string(config.msgpack.max_payload_size) +
			" is very large (>100MB), consider reducing for DoS protection");
	}

	if (!r.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "[config] server validation failed: {}", r.errors);
	}
	if (!r.warnings.empty()) {
		if (auto* l = GetLogger()) ENGINE_LOG_WARN(l, "[config] server validation warning: {}", r.warnings);
	}

	return r;
}

// ── ClientConfig validation ───────────────────────────────────────────────

ConfigValidator::Result ConfigValidator::ValidateClient(const ClientConfig& config) {
	Result r;

	CheckNotEmpty(r, config.scripts_dir, "scripts_dir");

	return r;
}

// ── Bulk validation helpers ───────────────────────────────────────────────

ConfigValidator::Result ConfigValidator::ValidateAll(
		const RuntimeConfig& runtime,
		const ClientConfig& client,
		const ServerConfig& server) {
	Result combined;
	MergeResult(combined, Validate(runtime));
	MergeResult(combined, ValidateClient(client));
	MergeResult(combined, ValidateServer(server));
	MergeResult(combined, ValidateCross(runtime, server));
	return combined;
}

ConfigValidator::Result ConfigValidator::ValidateCross(
		const RuntimeConfig& runtime,
		const ServerConfig& server) {
	Result r;

	// Cross-field: if admin_port is non-zero, scripts_dir must be set
	// (admin HTTP ships with default scripts that need the VM)
	if (server.admin_port != 0 && runtime.scripts_dir.empty()) {
		r.valid = false;
		r.errors += "admin_port is enabled but scripts_dir is empty";
	}

	return r;
}

// ── Internal check helpers ────────────────────────────────────────────────

void ConfigValidator::CheckRange(Result& r, int64_t value, int64_t min,
								  int64_t max, const std::string& field) {
	if (value < min || value > max) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += field + " (" + std::to_string(value) + ") must be in [" +
					std::to_string(min) + ", " + std::to_string(max) + "]";
	}
}

void ConfigValidator::CheckNotEmpty(Result& r, const std::string& value,
									 const std::string& field) {
	if (value.empty()) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += field + " must not be empty";
	}
}

void ConfigValidator::CheckEnum(Result& r, const std::string& value,
								 const std::unordered_set<std::string>& allowed,
								 const std::string& field) {
	if (allowed.find(value) == allowed.end()) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += field + " (" + value + ") is not a recognized value";
	}
}

void ConfigValidator::CheckWarning(Result& r, bool condition,
									const std::string& message) {
	if (condition) {
		if (!r.warnings.empty()) r.warnings += "; ";
		r.warnings += message;
	}
}

}  // namespace engine
