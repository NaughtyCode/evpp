#pragma once

#include <string>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/vm/sandbox.h"

namespace engine {

struct LuaScriptValidationResult {
	enum class Status {
		Ok,
		FileNotFound,
		VmCreateFailed,
		CompileError,
		RuntimeError,
	};

	Status status = Status::Ok;
	std::string filepath;
	std::string error;

	bool ok() const {
		return status == Status::Ok;
	}
};

class CLOUD_ENGINE_API LuaScriptValidator {
	public:
	LuaScriptValidator() = default;

	void SetScriptDirs(std::vector<std::string> script_dirs);
	void SetSandboxLevel(LuaSandboxLevel level);

	// Compile and run a script in a dedicated, isolated ScriptVM.
	// The target runtime VM is never touched by validation.
	LuaScriptValidationResult ValidateFile(const std::string& filepath) const;

	private:
	std::vector<std::string> script_dirs_;
	LuaSandboxLevel sandbox_level_ = LuaSandboxLevel::Strict;
};

}  // namespace engine
