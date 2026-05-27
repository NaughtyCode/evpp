#pragma once

#include <string>

#include "runtime/config/config.h"
#include "runtime/core/engine_api.h"

namespace engine {

// Post-parse validation for RuntimeConfig. Call after glaze::read_json()
// to verify field ranges, required fields, and type constraints that JSON
// syntax checking alone cannot catch.
class ENGINE_API ConfigValidator {
	public:
	struct Result {
		bool valid = true;
		std::string errors;  // accumulated error messages
	};

	static Result Validate(const RuntimeConfig& config);

	private:
	static void CheckRange(Result& r, int64_t value, int64_t min, int64_t max,
						   const std::string& field);
	static void CheckNotEmpty(Result& r, const std::string& value,
							  const std::string& field);
};

}  // namespace engine
