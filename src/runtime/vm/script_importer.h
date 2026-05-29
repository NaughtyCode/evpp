#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

// ScriptImporter — per-VM Lua module import system
//
// Module names are dot-separated paths relative to search directories:
//   import("utils.helpers")  →  <search_dir>/utils/helpers.lua
//   import("utils.*")        →  loads all .lua files in <search_dir>/utils/

class CLOUD_ENGINE_API ScriptImporter {
	public:
	ScriptImporter() = default;

	// Set the initial search directories. Call once during engine init.
	void Init(std::string scripts_dir);

	// Import a module by name.
	//   "sub.mod"  → load <search_dir>/sub/mod.lua, cache in package.loaded
	//   "sub.*"    → load all .lua files in <search_dir>/sub/, return table
	int Import(lua_State* L, std::string_view name);

	// Configure search directories (semicolon-separated).
	void SetPaths(const std::string& paths);
	void AddPath(const std::string& path);

	// Clear the module cache in package.loaded (enables hot-reload).
	void ClearCache(lua_State* L);

	private:
	struct ModuleGlobals {
		std::string module_name;
		std::vector<std::string> global_keys;
	};

	static std::string ModuleToPath(std::string_view name);
	std::string FindModule(std::string_view name) const;
	std::string FindDir(std::string_view dir_name) const;

	int ImportSingle(lua_State* L, std::string_view name);
	int ImportAll(lua_State* L, std::string_view name);

	std::string FormatImportStack() const;

	// Snapshot all keys in the global table _G.
	static std::vector<std::string> SnapshotGlobalKeys(lua_State* L);

	// Track new globals set by a module and warn about global pollution.
	void TrackNewGlobals(lua_State* L, const std::string& module_name,
						 const std::vector<std::string>& before);

	std::vector<std::string> search_paths_;
	std::unordered_set<std::string> importing_;
	std::unordered_map<std::string, ModuleGlobals> module_globals_;
};

}  // namespace engine
