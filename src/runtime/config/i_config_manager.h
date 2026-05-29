#pragma once

#include <string>

namespace engine {

// Unified validation result used by all config managers.
struct ValidationResult {
	bool valid = true;
	std::string errors;    // accumulated error messages
	std::string warnings;  // non-fatal diagnostics
};

// Abstract interface for config managers — allows ConfigManager and
// PhysicsConfigManager to be used polymorphically for loading, reloading,
// validation, and snapshot/dump.
class IConfigManager {
public:
	virtual ~IConfigManager() = default;
	virtual bool Load(const std::string& config_dir) = 0;
	virtual bool Reload(const std::string& config_dir) = 0;
	virtual ValidationResult Validate() const = 0;
	virtual std::string Dump() const = 0;
};

}  // namespace engine
