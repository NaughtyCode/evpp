#include "runtime/config/config_validator.h"

#include <cctype>
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

const std::unordered_set<std::string> kValidEnvironments = {
	"development", "dev", "staging", "stage", "production", "prod"
};

const std::unordered_set<std::string> kValidMongoSelections = {
	"", "dev", "public"
};

const std::unordered_set<std::string> kValidLogRotationFrequencies = {
	"", "daily", "hourly", "minutely"
};

const std::unordered_set<std::string> kValidLogRotationNamingSchemes = {
	"index", "date", "date_and_time"
};

const std::unordered_set<std::string> kValidRenderBackends = {
	"opengl", "vulkan", "directx11", "directx12", "metal"
};

const std::unordered_set<std::string> kValidAudioBackends = {
	"openal", "xaudio2", "coreaudio", "wasapi"
};

const std::unordered_set<std::string> kValidTextureQualities = {
	"low", "medium", "high", "ultra"
};

const std::unordered_set<std::string> kValidColorBlindModes = {
	"none", "protanopia", "deuteranopia", "tritanopia"
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
	CheckEnum(r, config.environment, kValidEnvironments, "environment");

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
	CheckEnum(r, config.log.rotation_frequency, kValidLogRotationFrequencies,
			  "log.rotation_frequency");
	CheckEnum(r, config.log.rotation_naming_scheme, kValidLogRotationNamingSchemes,
			  "log.rotation_naming_scheme");
	CheckRange(r, config.log.rotation_interval, 1, 1440, "log.rotation_interval");
	if (!config.log.rotation_time_daily.empty()) {
		const bool valid_time =
			config.log.rotation_time_daily.size() == 5 &&
			config.log.rotation_time_daily[2] == ':' &&
			std::isdigit(static_cast<unsigned char>(config.log.rotation_time_daily[0])) &&
			std::isdigit(static_cast<unsigned char>(config.log.rotation_time_daily[1])) &&
			std::isdigit(static_cast<unsigned char>(config.log.rotation_time_daily[3])) &&
			std::isdigit(static_cast<unsigned char>(config.log.rotation_time_daily[4]));
		int hour = valid_time ? std::stoi(config.log.rotation_time_daily.substr(0, 2)) : -1;
		int minute = valid_time ? std::stoi(config.log.rotation_time_daily.substr(3, 2)) : -1;
		if (!valid_time || hour > 23 || minute > 59) {
			r.valid = false;
			if (!r.errors.empty()) r.errors += "; ";
			r.errors += "log.rotation_time_daily must be HH:MM";
		}
	}

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
		CheckNotEmpty(r, config.admin_bind_address, "admin_bind_address");
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


	// resource_limits
	if (config.resource_limits.max_message_size == 0) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "resource_limits.max_message_size must be > 0 (0 would reject all messages)";
	}
	if (config.resource_limits.max_buffer_capacity == 0) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "resource_limits.max_buffer_capacity must be > 0";
	}
	if (config.resource_limits.max_http_body_size == 0) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "resource_limits.max_http_body_size must be > 0 (0 would reject all HTTP bodies)";
	}
	if (config.resource_limits.max_msgpack_depth == 0) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "resource_limits.max_msgpack_depth must be > 0";
	}
	if (config.resource_limits.max_buffer_capacity < config.resource_limits.max_message_size) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "resource_limits.max_buffer_capacity must be >= max_message_size";
	}
	CheckRange(r, config.shutdown_timeout_sec, 1, 3600, "shutdown_timeout_sec");
	CheckRange(r, config.connection_drain_timeout_sec, 0, 3600,
			   "connection_drain_timeout_sec");
	CheckRange(r, config.max_connections, 0, 10000000, "max_connections");
	CheckRange(r, config.config_webhook_timeout_sec, 1, 300,
			   "config_webhook_timeout_sec");
	CheckRange(r, config.tcp_keepalive.idle_sec, 0, 86400, "tcp_keepalive.idle_sec");
	CheckRange(r, config.tcp_keepalive.interval_sec, 0, 86400,
			   "tcp_keepalive.interval_sec");
	CheckRange(r, config.tcp_keepalive.count, 0, 100, "tcp_keepalive.count");
	CheckEnum(r, config.active_mongodb, kValidMongoSelections, "active_mongodb");
	if (config.resource_limits.max_message_size > 1024 * 1024 * 1024) {
		CheckWarning(r, true,
			"resource_limits.max_message_size=" + std::to_string(config.resource_limits.max_message_size) +
			" is very large (>1GB), consider reducing for DoS protection");
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

	// ── Render ────────────────────────────────────────────────────────
	CheckEnum(r, config.render.backend, kValidRenderBackends, "render.backend");
	CheckRange(r, config.render.resolution_width, 1, 16384, "render.resolution_width");
	CheckRange(r, config.render.resolution_height, 1, 16384, "render.resolution_height");
	CheckRange(r, config.render.max_fps, 1, 1000, "render.max_fps");
	if (config.render.msaa_samples != 0 && config.render.msaa_samples != 2 &&
		config.render.msaa_samples != 4 && config.render.msaa_samples != 8 &&
		config.render.msaa_samples != 16) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "render.msaa_samples (" + std::to_string(config.render.msaa_samples) +
					") must be 0, 2, 4, 8, or 16";
	}

	// ── Window ────────────────────────────────────────────────────────
	if (!config.window.title.empty() && config.window.title.size() > 256) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "window.title must be <= 256 characters";
	}
	CheckRange(r, config.window.width, 1, 16384, "window.width");
	CheckRange(r, config.window.height, 1, 16384, "window.height");

	// ── Input ─────────────────────────────────────────────────────────
	if (config.input.mouse_sensitivity < 0.01f || config.input.mouse_sensitivity > 100.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "input.mouse_sensitivity (" + std::to_string(config.input.mouse_sensitivity) +
					") must be in [0.01, 100.0]";
	}
	if (config.input.gamepad_deadzone < 0.0f || config.input.gamepad_deadzone > 1.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "input.gamepad_deadzone (" + std::to_string(config.input.gamepad_deadzone) +
					") must be in [0.0, 1.0]";
	}

	// ── Audio ─────────────────────────────────────────────────────────
	CheckEnum(r, config.audio.backend, kValidAudioBackends, "audio.backend");
	CheckRange(r, config.audio.sample_rate, 8000, 384000, "audio.sample_rate");
	CheckRange(r, config.audio.channels, 1, 16, "audio.channels");
	if (config.audio.master_volume < 0.0f || config.audio.master_volume > 1.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "audio.master_volume (" + std::to_string(config.audio.master_volume) +
					") must be in [0.0, 1.0]";
	}
	if (config.audio.music_volume < 0.0f || config.audio.music_volume > 1.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "audio.music_volume must be in [0.0, 1.0]";
	}
	if (config.audio.sfx_volume < 0.0f || config.audio.sfx_volume > 1.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "audio.sfx_volume must be in [0.0, 1.0]";
	}

	// ── Network ───────────────────────────────────────────────────────
	if (config.network.server_port != 0) {
		CheckRange(r, config.network.server_port, 1, 65535, "network.server_port");
	}
	CheckRange(r, config.network.timeout_ms, 100, 300000, "network.timeout_ms");
	CheckRange(r, config.network.reconnect_max_retries, 0, 1000, "network.reconnect_max_retries");
	if (config.network.reconnect_base_delay_ms > config.network.reconnect_max_delay_ms) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "network.reconnect_base_delay_ms must be <= reconnect_max_delay_ms";
	}
	CheckRange(r, config.network.interpolation_delay_ms, 0, 5000, "network.interpolation_delay_ms");

	// ── Assets ────────────────────────────────────────────────────────
	CheckEnum(r, config.assets.texture_quality, kValidTextureQualities, "assets.texture_quality");
	CheckRange(r, config.assets.streaming_budget_mb, 0, 65536, "assets.streaming_budget_mb");
	if (config.assets.lod_bias < 0.0f || config.assets.lod_bias > 10.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "assets.lod_bias must be in [0.0, 10.0]";
	}

	// ── UI ────────────────────────────────────────────────────────────
	CheckEnum(r, config.ui.color_blind_mode, kValidColorBlindModes, "ui.color_blind_mode");
	CheckRange(r, config.ui.font_size, 1, 256, "ui.font_size");
	if (config.ui.scale < 0.25f || config.ui.scale > 10.0f) {
		r.valid = false;
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "ui.scale (" + std::to_string(config.ui.scale) +
					") must be in [0.25, 10.0]";
	}

	// ── Warnings ──────────────────────────────────────────────────────
	if (config.audio.master_volume > 0.0f && config.audio.master_volume < 0.01f) {
		CheckWarning(r, true, "audio.master_volume is effectively muted (< 0.01)");
	}
	if (config.network.server_port != 0 && config.network.server_port < 1024) {
		CheckWarning(r, true, "network.server_port < 1024 may require elevated privileges");
	}

	if (!r.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "[config] client validation failed: {}", r.errors);
	}
	if (!r.warnings.empty()) {
		if (auto* l = GetLogger()) ENGINE_LOG_WARN(l, "[config] client validation warning: {}", r.warnings);
	}

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
