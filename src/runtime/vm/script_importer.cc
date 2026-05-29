#include "runtime/vm/script_importer.h"

#include <algorithm>
#include <filesystem>

#include "runtime/core/log/log.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

// ScriptImporter implementation

void ScriptImporter::Init(std::string scripts_dir) {
	search_paths_.clear();

	if (!scripts_dir.empty()) {
#ifdef _WIN32
		std::replace(scripts_dir.begin(), scripts_dir.end(), '\\', '/');
#endif
		if (scripts_dir.back() != '/') {
			scripts_dir += '/';
		}
		search_paths_.push_back(scripts_dir);
	}

	// Always include a "./" fallback for relative imports
	search_paths_.push_back("./");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: initialized with [{}] path(s)", search_paths_.size());
	for (const auto& p : search_paths_) {
		ENGINE_LOG_INFO(logger, "import:   path [{}]", p);
	}
}

int ScriptImporter::Import(lua_State* L, std::string_view name) {
	// ── Wildcard: "subdir.*" loads all .lua files in that directory ──
	if (name.size() >= 2 && name.substr(name.size() - 2) == ".*") {
		return ImportAll(L, name);
	}
	return ImportSingle(L, name);
}

int ScriptImporter::ImportSingle(lua_State* L, std::string_view name) {
	std::string name_str(name);

	// Circular dependency detection
	if (importing_.count(name_str)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
						 "Circular dependency detected: module '{}' is already being imported. "
						 "Import stack: {}",
						 name_str,
						 FormatImportStack());
		return luaL_error(L, "circular dependency detected: module '%s'", name_str.c_str());
	}
	importing_.insert(name_str);

	// RAII cleanup: remove from importing set on scope exit.
	// luaL_error does a longjmp, so we must erase before those calls.
	auto cleanup_importing = [this, &name_str]() {
		importing_.erase(name_str);
	};

	// ── Check package.loaded cache ──────────────────────────────────
	lua_getglobal(L, "package");  // ..., pkg
	lua_getfield(L, -1, "loaded");	// ..., pkg, loaded
	lua_getfield(L, -1, name_str.c_str());	// ..., pkg, loaded, cached

	if (!lua_isnil(L, -1)) {
		lua_remove(L, -3);	// ..., loaded, cached
		lua_remove(L, -2);	// ..., cached
		cleanup_importing();
		return 1;
	}
	lua_pop(L, 1);	// pop nil

	// ── Find the module file ───────────────────────────────────────
	std::string filepath = FindModule(name);
	if (filepath.empty()) {
		lua_pop(L, 2);	// pop loaded, package
		cleanup_importing();
		return luaL_error(L, "module '%s' not found in import paths", name_str.c_str());
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: loading module [{}] from [{}]", name_str, filepath);

	// ── Load the Lua chunk ─────────────────────────────────────────
	int rc = luaL_loadfilex(L, filepath.c_str(), nullptr);
	if (rc != LUA_OK) {
		std::string err_msg(lua_tostring(L, -1));
		lua_pop(L, 1);	// pop error message
		lua_pop(L, 2);	// pop loaded, package
		cleanup_importing();
		return luaL_error(L, "error loading module '%s': %s", name_str.c_str(), err_msg.c_str());
	}

	// ── Execute the chunk ──────────────────────────────────────────
	auto before_keys = SnapshotGlobalKeys(L);
	rc = lua_pcall(L, 0, 1, 0);
	if (rc != LUA_OK) {
		std::string err_msg(lua_tostring(L, -1));
		lua_pop(L, 1);	// pop error message
		lua_pop(L, 2);	// pop loaded, package
		cleanup_importing();
		return luaL_error(L, "error running module '%s': %s", name_str.c_str(), err_msg.c_str());
	}

	// Track new globals set by this module
	TrackNewGlobals(L, name_str, before_keys);

	// ── Cache result in package.loaded ─────────────────────────────
	// Stack: ..., pkg, loaded, result
	lua_pushvalue(L, -1);  // ..., pkg, loaded, result, copy
	lua_setfield(L, -3, name_str.c_str());	// loaded[name] = copy
	lua_remove(L, -3);	// ..., pkg, result
	lua_remove(L, -2);	// ..., result
	cleanup_importing();
	return 1;
}

std::string ScriptImporter::FormatImportStack() const {
	std::string stack;
	for (const auto& m : importing_) {
		if (!stack.empty()) stack += " -> ";
		stack += m;
	}
	return stack.empty() ? "(empty)" : stack;
}

int ScriptImporter::ImportAll(lua_State* L, std::string_view name) {
	// name ends with ".*" — strip it to get the directory portion
	std::string dir_name(name.substr(0, name.size() - 2));
	std::string dir_path = ModuleToPath(dir_name);
	std::string full_dir = FindDir(dir_name);

	if (full_dir.empty()) {
		return luaL_error(L,
						  "import '%s': directory '%s' not found in import paths",
						  std::string(name).c_str(),
						  dir_path.c_str());
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "import: loading all modules from [{}]", full_dir);

	std::error_code ec;
	int count = 0;

	// ── Result table ────────────────────────────────────────────────
	lua_newtable(L);
	int table_idx = lua_gettop(L);

	for (const auto& entry : std::filesystem::directory_iterator(full_dir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;

		auto ext = entry.path().extension().string();
		if (ext != ".lua" && ext != ".LUA") continue;

		std::string stem = entry.path().stem().string();
		std::string filepath = entry.path().string();

		ENGINE_LOG_INFO(logger, "import:   loading [{}]", filepath);

		// Load
		int rc = luaL_loadfilex(L, filepath.c_str(), nullptr);
		if (rc != LUA_OK) {
			const char* msg = lua_tostring(L, -1);
			ENGINE_LOG_WARN(logger, "import:   error loading [{}]: {}", filepath, msg);
			lua_pop(L, 1);
			continue;
		}

		// Execute
		auto before_keys = SnapshotGlobalKeys(L);
		rc = lua_pcall(L, 0, 1, 0);
		if (rc != LUA_OK) {
			const char* msg = lua_tostring(L, -1);
			ENGINE_LOG_WARN(logger, "import:   error running [{}]: {}", filepath, msg);
			lua_pop(L, 1);
			continue;
		}

		std::string cache_name = dir_name.empty() ? stem : dir_name + "." + stem;
		TrackNewGlobals(L, cache_name, before_keys);

		// result[stem] = module result
		lua_setfield(L, table_idx, stem.c_str());

		// Cache in package.loaded as "dir.stem"
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "loaded");
		lua_pushvalue(L, table_idx);  // push result table
		lua_getfield(L, -1, stem.c_str());	// get result[stem]
		lua_setfield(L, -3, cache_name.c_str());  // package.loaded[cache_name] = result
		lua_pop(L, 3);	// pop result_table_copy, loaded, package

		++count;
	}

	ENGINE_LOG_INFO(logger, "import: loaded [{}] module(s) from [{}]", count, full_dir);
	return 1;  // return the result table
}

void ScriptImporter::SetPaths(const std::string& paths) {
	search_paths_.clear();
	size_t start = 0;
	while (start < paths.size()) {
		size_t end = paths.find(';', start);
		if (end == std::string::npos) end = paths.size();
		std::string path = paths.substr(start, end - start);
		if (!path.empty()) {
#ifdef _WIN32
			std::replace(path.begin(), path.end(), '\\', '/');
#endif
			if (path.back() != '/') {
				path += '/';
			}
			search_paths_.push_back(path);
		}
		start = end + 1;
	}
}

void ScriptImporter::AddPath(const std::string& path) {
	std::string p(path);
#ifdef _WIN32
	std::replace(p.begin(), p.end(), '\\', '/');
#endif
	if (!p.empty() && p.back() != '/') {
		p += '/';
	}
	if (!p.empty()) {
		search_paths_.push_back(p);
	}
}

std::vector<std::string> ScriptImporter::SnapshotGlobalKeys(lua_State* L) {
	std::vector<std::string> keys;
	lua_pushglobaltable(L);	// ..., _G
	lua_pushnil(L);	// ..., _G, nil
	while (lua_next(L, -2) != 0) {
		// ..., _G, key, value
		if (lua_type(L, -2) == LUA_TSTRING) {
			keys.push_back(lua_tostring(L, -2));
		}
		lua_pop(L, 1);	// pop value, keep key for next iteration
	}
	lua_pop(L, 1);	// pop _G
	return keys;
}

void ScriptImporter::TrackNewGlobals(lua_State* L, const std::string& module_name,
									 const std::vector<std::string>& before) {
	auto after = SnapshotGlobalKeys(L);
	ModuleGlobals mg;
	mg.module_name = module_name;

	// Find keys that exist in 'after' but not in 'before'
	for (auto& key : after) {
		if (std::find(before.begin(), before.end(), key) == before.end()) {
			mg.global_keys.push_back(key);
			auto* logger = GetLogger();
			ENGINE_LOG_WARN(logger,
						   "Module '{}' sets global '{}' — prefer returning a local table",
						   module_name, key);
		}
	}

	if (!mg.global_keys.empty()) {
		module_globals_[module_name] = std::move(mg);
	}
}

void ScriptImporter::ClearCache(lua_State* L) {
	// Nil all tracked globals before clearing package.loaded
	for (auto& [name, mg] : module_globals_) {
		for (auto& key : mg.global_keys) {
			lua_pushnil(L);
			lua_setglobal(L, key.c_str());
		}
	}
	module_globals_.clear();

	lua_getglobal(L, "package");  // ..., package
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_newtable(L);  // ..., package, new_loaded
	lua_setfield(L, -2, "loaded");	// package.loaded = {}
	lua_pop(L, 1);	// ...
}

std::string ScriptImporter::ModuleToPath(std::string_view name) {
	std::string result(name);
	for (auto& c : result) {
		if (c == '.') {
			c = '/';
		}
	}
	return result;
}

std::string ScriptImporter::FindModule(std::string_view name) const {
	std::string mod_path = ModuleToPath(name) + ".lua";
	std::error_code ec;

	for (const auto& base : search_paths_) {
		std::string path = base + mod_path;
		ec.clear();
		if (std::filesystem::is_regular_file(path, ec) && !ec) {
			return path;
		}
	}
	return {};
}

std::string ScriptImporter::FindDir(std::string_view dir_name) const {
	std::string mod_dir = ModuleToPath(dir_name);
	std::error_code ec;

	for (const auto& base : search_paths_) {
		std::string path = base + mod_dir;
		ec.clear();
		if (std::filesystem::is_directory(path, ec) && !ec) {
			return path;
		}
	}
	return {};
}

}  // namespace engine
