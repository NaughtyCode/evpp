#include "runtime/vm/script_validator.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "runtime/script/bind/import_bind.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/script_importer.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

namespace {

std::string NormalizeLuaPath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (!path.empty() && path.back() != '/') {
		path += '/';
	}
	return path;
}

std::vector<std::string> BuildSearchDirs(const std::vector<std::string>& dirs) {
	std::vector<std::string> result;
	result.reserve(dirs.size() + 1);
	for (const auto& dir : dirs) {
		if (dir.empty()) continue;
		result.push_back(NormalizeLuaPath(dir));
	}
	result.push_back("./");
	return result;
}

std::string BuildPackagePath(const std::vector<std::string>& dirs) {
	std::string package_path;
	for (const auto& dir : BuildSearchDirs(dirs)) {
		if (!package_path.empty()) package_path += ";";
		package_path += dir + "?.lua;" + dir + "?/init.lua";
	}
	return package_path;
}

std::string BuildImportPath(const std::vector<std::string>& dirs) {
	std::string import_path;
	for (const auto& dir : BuildSearchDirs(dirs)) {
		if (!import_path.empty()) import_path += ";";
		import_path += dir;
	}
	return import_path;
}

std::string LuaStackMessage(lua_State* L, const char* fallback) {
	const char* msg = lua_tostring(L, -1);
	return msg ? msg : fallback;
}

void ConfigurePackagePath(lua_State* L, const std::vector<std::string>& dirs) {
	std::string package_path = BuildPackagePath(dirs);

	lua_getglobal(L, "package");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	lua_getfield(L, -1, "path");
	const char* existing_path = lua_tostring(L, -1);
	if (existing_path && existing_path[0] != '\0') {
		package_path += ";";
		package_path += existing_path;
	}
	lua_pop(L, 1);

	lua_pushstring(L, package_path.c_str());
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
}

void ConfigureImport(ScriptVM& vm, const std::vector<std::string>& dirs) {
	vm.GetImporter().SetPaths(BuildImportPath(dirs));
	ExportImport(vm);
}

}  // namespace

void LuaScriptValidator::SetScriptDirs(std::vector<std::string> script_dirs) {
	script_dirs_ = std::move(script_dirs);
}

void LuaScriptValidator::SetSandboxLevel(LuaSandboxLevel level) {
	sandbox_level_ = level;
}

LuaScriptValidationResult LuaScriptValidator::ValidateFile(
	const std::string& filepath) const {
	LuaScriptValidationResult result;
	result.filepath = filepath;

	std::error_code ec;
	if (!std::filesystem::is_regular_file(filepath, ec) || ec) {
		result.status = LuaScriptValidationResult::Status::FileNotFound;
		result.error = ec ? ec.message() : "file not found";
		return result;
	}

	std::unique_ptr<ScriptVM> validation_vm;
	try {
		validation_vm = std::make_unique<ScriptVM>(sandbox_level_);
	} catch (const std::exception& e) {
		result.status = LuaScriptValidationResult::Status::VmCreateFailed;
		result.error = e.what();
		return result;
	} catch (...) {
		result.status = LuaScriptValidationResult::Status::VmCreateFailed;
		result.error = "unknown VM creation failure";
		return result;
	}

	lua_State* L = validation_vm->GetState();
	if (!L) {
		result.status = LuaScriptValidationResult::Status::VmCreateFailed;
		result.error = "validation VM has no Lua state";
		return result;
	}

	ConfigurePackagePath(L, script_dirs_);
	validation_vm->SetScriptRoots(script_dirs_);
	ConfigureImport(*validation_vm, script_dirs_);

	const int base_top = lua_gettop(L);
	int rc = luaL_loadfilex(L, filepath.c_str(), "t");
	if (rc != LUA_OK) {
		result.status = LuaScriptValidationResult::Status::CompileError;
		result.error = LuaStackMessage(L, "unknown Lua compile error");
		lua_settop(L, base_top);
		return result;
	}

	const int msgh = PushLuaErrorHandlerForCall(L, 0);
	rc = lua_pcall(L, 0, 0, msgh);
	if (rc != LUA_OK) {
		result.status = LuaScriptValidationResult::Status::RuntimeError;
		result.error = LuaStackMessage(L, "unknown Lua runtime error");
		lua_settop(L, base_top);
		return result;
	}
	lua_remove(L, msgh);
	lua_settop(L, base_top);

	return result;
}

}  // namespace engine
