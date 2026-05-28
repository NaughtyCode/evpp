#include "runtime/vm/script_reloader.h"

#include <chrono>
#include <filesystem>

#include "runtime/core/log/log.h"
#include "runtime/vm/file_watcher.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/script_importer.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

ScriptReloader::ScriptReloader() = default;

ScriptReloader::~ScriptReloader() {
	Stop();
}

void ScriptReloader::SetTarget(ScriptVM* vm,
                                const std::vector<std::string>& script_dirs) {
	vm_ = vm;
	script_dirs_ = script_dirs;
}

void ScriptReloader::Start(int poll_interval_ms, int debounce_ms) {
	if (!vm_) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ScriptReloader: SetTarget() must be called before Start()");
		return;
	}

	debounce_ms_ = debounce_ms;
	last_reload_time_ = std::chrono::steady_clock::now();

	watcher_ = std::make_unique<FileWatcher>();
	for (const auto& dir : script_dirs_) {
		watcher_->WatchDirectory(dir, ".lua");
	}

	watcher_->SetChangeCallback(
		[this](const std::vector<std::string>& files) { OnFilesChanged(files); });

	watcher_->Start(poll_interval_ms);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ScriptReloader: started, watching [{}] dir(s), debounce=[{}ms]",
					script_dirs_.size(),
					debounce_ms);
}

void ScriptReloader::Stop() {
	if (watcher_) {
		watcher_->Stop();
		watcher_.reset();
	}
}

void ScriptReloader::OnFilesChanged(const std::vector<std::string>& files) {
	// Debounce: skip if within the debounce window
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					   now - last_reload_time_)
					   .count();
	if (elapsed < debounce_ms_) return;

	last_reload_time_ = now;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ScriptReloader: [{}] file(s) changed, triggering reload",
					files.size());

	// Validate and reload each changed file
	bool all_ok = true;
	for (const auto& file : files) {
		if (!ValidateScript(file)) {
			ENGINE_LOG_ERROR(logger,
							 "ScriptReloader: validation failed for [{}], "
							 "skipping",
							 file);
			all_ok = false;
			if (reload_callback_) reload_callback_(file, false);
			continue;
		}

		if (!ReloadFile(file)) {
			ENGINE_LOG_ERROR(logger, "ScriptReloader: reload failed for [{}]", file);
			all_ok = false;
			if (reload_callback_) reload_callback_(file, false);
			continue;
		}

		ENGINE_LOG_INFO(logger, "ScriptReloader: reloaded [{}] successfully", file);
		if (reload_callback_) reload_callback_(file, true);
	}

	if (all_ok) {
		ENGINE_LOG_INFO(logger, "ScriptReloader: all [{}] file(s) reloaded OK", files.size());
	}
}

bool ScriptReloader::ValidateScript(const std::string& filepath) {
	// Create a temporary Lua VM for isolated validation.
	// This VM shares no state with the real ScriptVM, so syntax errors
	// and load-time errors here do not affect the running system.

	auto* logger = GetLogger();

	if (!std::filesystem::exists(filepath)) {
		ENGINE_LOG_WARN(logger, "ScriptReloader: file not found [{}]", filepath);
		return false;
	}

	lua_State* L = luaL_newstate();
	if (!L) {
		ENGINE_LOG_ERROR(logger, "ScriptReloader: failed to create validation Lua state");
		return false;
	}
	luaL_openlibs_sandboxed(L, LuaSandboxLevel::Strict);

	// Load the file as a Lua chunk. Do NOT execute — just compile.
	int ret = luaL_loadfile(L, filepath.c_str());
	if (ret != LUA_OK) {
		ENGINE_LOG_WARN(logger,
						"ScriptReloader: validation failed for [{}]: {}",
						filepath,
						lua_tostring(L, -1));
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	// Optionally run the chunk to catch runtime errors in top-level code.
	// This is a trade-off: running in sandbox may have side effects
	// (e.g., globals), but catches more errors than compile-only.
	// We run in the sandbox so side effects are isolated.
	ret = lua_pcall(L, 0, 0, 0);
	if (ret != LUA_OK) {
		ENGINE_LOG_WARN(logger,
						"ScriptReloader: runtime validation failed for [{}]: {}",
						filepath,
						lua_tostring(L, -1));
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	lua_close(L);
	return true;
}

bool ScriptReloader::ReloadFile(const std::string& filepath) {
	if (!vm_) return false;

	auto* L = vm_->GetState();
	if (!L) return false;

	// Snapshot globals before reload (best-effort rollback)
	SnapshotGlobals(L);

	// Clear the import cache so the file is re-loaded fresh
	vm_->GetImporter().ClearCache(L);

	// Extract module name from file path
	std::string module_name =
		std::filesystem::path(filepath).stem().string();

	// Remove the module from package.loaded so it gets re-imported
	lua_getglobal(L, "package");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "loaded");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			lua_setfield(L, -2, module_name.c_str());
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	// Load and execute the file
	int ret = luaL_loadfile(L, filepath.c_str());
	if (ret != LUA_OK) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
						 "ScriptReloader: load failed for [{}]: {}",
						 filepath,
						 lua_tostring(L, -1));
		lua_pop(L, 1);
		RestoreGlobals(L);
		return false;
	}

	// Run the chunk
	ret = lua_pcall(L, 0, 1, 0);
	if (ret != LUA_OK) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
						 "ScriptReloader: execute failed for [{}]: {}",
						 filepath,
						 lua_tostring(L, -1));
		lua_pop(L, 1);
		RestoreGlobals(L);
		return false;
	}

	// If the module returned a table, register it in package.loaded
	if (lua_istable(L, -1)) {
		lua_getglobal(L, "package");
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "loaded");
			if (lua_istable(L, -1)) {
				lua_pushvalue(L, -3);
				lua_setfield(L, -2, module_name.c_str());
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);  // pop return value

	return true;
}

bool ScriptReloader::ReloadAll() {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptReloader: reloading all scripts...");

	bool all_ok = true;
	for (const auto& dir : script_dirs_) {
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
			 it != std::filesystem::recursive_directory_iterator(); ++it) {
			if (ec) break;
			const auto& entry = *it;
			if (entry.is_regular_file() && entry.path().extension() == ".lua") {
				if (!ReloadFile(entry.path().string())) {
					all_ok = false;
				}
			}
		}
	}

	if (all_ok) {
		ENGINE_LOG_INFO(logger, "ScriptReloader: all scripts reloaded OK");
	} else {
		ENGINE_LOG_ERROR(logger, "ScriptReloader: some scripts failed to reload");
	}

	return all_ok;
}

void ScriptReloader::SetReloadCallback(ReloadCallback callback) {
	reload_callback_ = std::move(callback);
}

void ScriptReloader::SnapshotGlobals(lua_State* L) {
	global_snapshot_.clear();

	lua_pushglobaltable(L);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char* key = lua_tostring(L, -2);
			if (key) {
				// Best-effort: snapshot string/number/boolean values
				if (lua_isstring(L, -1)) {
					global_snapshot_[key] = lua_tostring(L, -1);
				} else if (lua_isinteger(L, -1)) {
					global_snapshot_[key] = std::to_string(lua_tointeger(L, -1));
				} else if (lua_isnumber(L, -1)) {
					global_snapshot_[key] = std::to_string(lua_tonumber(L, -1));
				} else if (lua_isboolean(L, -1)) {
					global_snapshot_[key] = lua_toboolean(L, -1) ? "true" : "false";
				}
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);  // global table
}

void ScriptReloader::RestoreGlobals(lua_State* L) {
	for (const auto& [key, value] : global_snapshot_) {
		lua_pushstring(L, value.c_str());
		lua_setglobal(L, key.c_str());
	}
}

}  // namespace engine
