#pragma once

#include <string>
#include <unordered_set>

#include "runtime/config/config.h"
#include "runtime/config/i_config_manager.h"
#include "runtime/core/engine_api.h"

namespace engine {

// Post-parse validation for all config types. Call after glaze::read_json()
// to verify field ranges, required fields, and type constraints that JSON
// syntax checking alone cannot catch.
class ENGINE_API ConfigValidator {
	public:
	// Result type alias — canonical definition in i_config_manager.h.
	using Result = ValidationResult;

	static Result Validate(const RuntimeConfig& config);
	static Result ValidateServer(const ServerConfig& config);
	static Result ValidateClient(const ClientConfig& config);

	// Returns a Result with errors for all that fail.
	static Result ValidateAll(const RuntimeConfig& runtime,
							  const ClientConfig& client,
							  const ServerConfig& server);

	// Cross-field checks between config sections.
	static Result ValidateCross(const RuntimeConfig& runtime,
								const ServerConfig& server);

	private:
	static void CheckRange(Result& r, int64_t value, int64_t min, int64_t max,
						   const std::string& field);
	static void CheckNotEmpty(Result& r, const std::string& value,
							  const std::string& field);
	static void CheckEnum(Result& r, const std::string& value,
						  const std::unordered_set<std::string>& allowed,
						  const std::string& field);
	static void CheckWarning(Result& r, bool condition,
							 const std::string& message);
};

}  // namespace engine
