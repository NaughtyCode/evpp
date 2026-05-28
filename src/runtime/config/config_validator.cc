#include "runtime/config/config_validator.h"

#include <cstdio>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

namespace engine {

ConfigValidator::Result ConfigValidator::Validate(const RuntimeConfig& config) {
	Result r;

	CheckRange(r, config.frame.target_fps, 0, 1000, "frame.target_fps");
	CheckRange(r, config.frame.interval_ms, 1, 10000, "frame.interval_ms");
	CheckNotEmpty(r, config.resource_dir, "resource_dir");
	CheckNotEmpty(r, config.scripts_dir, "scripts_dir");

	if (config.log.rotation_size_mb < 1 || config.log.rotation_size_mb > 10240) {
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += "log.rotation_size_mb must be in [1, 10240]";
		r.valid = false;
	}

	if (!r.valid) {
		if (auto* l = GetLogger()) ENGINE_LOG_ERROR(l, "[config] validation failed: {}", r.errors);
	}

	return r;
}

void ConfigValidator::CheckRange(Result& r, int64_t value, int64_t min, int64_t max,
								  const std::string& field) {
	if (value < min || value > max) {
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += field + " (" + std::to_string(value) + ") must be in [" +
					std::to_string(min) + ", " + std::to_string(max) + "]";
		r.valid = false;
	}
}

void ConfigValidator::CheckNotEmpty(Result& r, const std::string& value,
									 const std::string& field) {
	if (value.empty()) {
		if (!r.errors.empty()) r.errors += "; ";
		r.errors += field + " must not be empty";
		r.valid = false;
	}
}

}  // namespace engine
