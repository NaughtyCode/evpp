#include "runtime/vm/script_reloader.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "runtime/core/log/log.h"
#include "runtime/evpp/event_loop.h"
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

void ScriptReloader::SetEventLoop(evpp::EventLoop* loop) {
	loop_ = loop;
}

void ScriptReloader::Start(int poll_interval_ms, int debounce_ms) {
	if (!vm_) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
			"ScriptReloader: SetTarget() must be called before Start()");
		return;
	}

	// Stop any existing watcher and reset state for a clean restart.
	Stop();
	file_reload_times_.clear();
	global_snapshot_.clear();
	{
		std::lock_guard<std::mutex> lock(pending_mutex_);
		pending_reloads_.clear();
		pending_failures_.clear();
	}

	debounce_ms_ = debounce_ms;

	watcher_ = std::make_unique<FileWatcher>();
	for (const auto& dir : script_dirs_) {
		watcher_->WatchDirectory(dir, ".lua");
	}

	watcher_->SetChangeCallback(
		[this](const std::vector<std::string>& files) { OnFilesChanged(files); });

	// Prime known files so the first scan doesn't report all existing
	// scripts as changes (which would trigger unnecessary mass reload).
	watcher_->PrimeKnownFiles();
	watcher_->Start(poll_interval_ms);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: started, watching [{}] dir(s), "
	                "debounce=[{}ms], dispatch=[{}]",
	                script_dirs_.size(),
	                debounce_ms,
	                loop_ ? "event_loop" : "direct");
}

void ScriptReloader::Stop() {
	if (watcher_) {
		watcher_->Stop();
		watcher_.reset();
	}
}

//=============================================================================
// OnFilesChanged — called on watcher thread
//=============================================================================

void ScriptReloader::OnFilesChanged(const std::vector<std::string>& files) {
	auto* logger = GetLogger();

	// Per-file debounce: filter out files changed within the debounce window.
	auto now = std::chrono::steady_clock::now();
	std::vector<std::string> ready_files;
	for (const auto& file : files) {
		auto it = file_reload_times_.find(file);
		if (it != file_reload_times_.end()) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - it->second).count();
			if (elapsed < debounce_ms_) {
				ENGINE_LOG_DEBUG(logger,
					"ScriptReloader: debounce skip [{}] ({}ms < {}ms)",
					file, elapsed, debounce_ms_);
				continue;
			}
		}
		file_reload_times_[file] = now;
		ready_files.push_back(file);
	}

	if (ready_files.empty()) return;

	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: [{}] file(s) changed after debounce",
	                ready_files.size());

	// Validate each changed file in a sandbox VM (thread-safe).
	std::vector<std::string> valid_files;
	std::vector<std::string> failed_files;
	for (const auto& file : ready_files) {
		if (ValidateScript(file)) {
			valid_files.push_back(file);
		} else {
			ENGINE_LOG_ERROR(logger,
				"ScriptReloader: validation failed for [{}], skipping",
				file);
			failed_files.push_back(file);
		}
	}

	// Dispatch reload results to the main thread if an EventLoop is available.
	// All callback invocations must happen on the main thread for consistency.
	if (loop_) {
		if (!valid_files.empty()) {
			loop_->RunInLoop([this, files = std::move(valid_files)]() {
				ProcessReloadList(files);
			});
		}
		if (!failed_files.empty()) {
			loop_->RunInLoop([this, files = std::move(failed_files)]() {
				for (const auto& f : files) {
					if (reload_callback_) reload_callback_(f, false);
				}
			});
		}
	} else {
		// No event loop — enqueue for manual processing.
		// This path is safe only in single-threaded test environments.
		std::lock_guard<std::mutex> lock(pending_mutex_);
		pending_reloads_.insert(pending_reloads_.end(),
		                        valid_files.begin(), valid_files.end());
		pending_failures_.insert(pending_failures_.end(),
		                         failed_files.begin(), failed_files.end());
	}
}

//=============================================================================
// ProcessReloadList — called on main thread (via EventLoop or manual)
//=============================================================================

void ScriptReloader::ProcessReloadList(const std::vector<std::string>& files) {
	auto* logger = GetLogger();
	bool all_ok = true;

	for (const auto& file : files) {
		if (!ReloadFile(file)) {
			ENGINE_LOG_ERROR(logger,
				"ScriptReloader: reload failed for [{}]", file);
			all_ok = false;
			if (reload_callback_) reload_callback_(file, false);
		} else {
			ENGINE_LOG_INFO(logger,
				"ScriptReloader: reloaded [{}] successfully", file);
			if (reload_callback_) reload_callback_(file, true);
		}
	}

	if (all_ok && !files.empty()) {
		ENGINE_LOG_INFO(logger,
			"ScriptReloader: all [{}] file(s) reloaded OK", files.size());
	}
}

void ScriptReloader::ProcessPendingReloads() {
	std::vector<std::string> files;
	std::vector<std::string> failures;
	{
		std::lock_guard<std::mutex> lock(pending_mutex_);
		files.swap(pending_reloads_);
		failures.swap(pending_failures_);
	}
	for (const auto& f : failures) {
		if (reload_callback_) reload_callback_(f, false);
	}
	if (!files.empty()) {
		ProcessReloadList(files);
	}
}

//=============================================================================
// ValidateScript — runs on watcher thread (creates its own lua_State)
//=============================================================================

bool ScriptReloader::ValidateScript(const std::string& filepath) {
	auto* logger = GetLogger();

	if (!std::filesystem::exists(filepath)) {
		ENGINE_LOG_WARN(logger, "ScriptReloader: file not found [{}]", filepath);
		return false;
	}

	lua_State* L = luaL_newstate();
	if (!L) {
		ENGINE_LOG_ERROR(logger,
			"ScriptReloader: failed to create validation Lua state");
		return false;
	}
	luaL_openlibs_sandboxed(L, LuaSandboxLevel::Strict);

	// Load the file as a Lua chunk and execute in the sandbox.
	// Using pcall to catch both compile-time and top-level runtime errors.
	int ret = luaL_loadfile(L, filepath.c_str());
	if (ret != LUA_OK) {
		ENGINE_LOG_WARN(logger,
		                "ScriptReloader: validation compile failed [{}]: {}",
		                filepath,
		                lua_tostring(L, -1));
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	ret = lua_pcall(L, 0, 0, 0);
	if (ret != LUA_OK) {
		ENGINE_LOG_WARN(logger,
		                "ScriptReloader: validation runtime failed [{}]: {}",
		                filepath,
		                lua_tostring(L, -1));
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	lua_close(L);
	return true;
}

//=============================================================================
// Module name extraction helper
//=============================================================================

static std::string ExtractModuleName(const std::string& filepath,
                                      const std::vector<std::string>& script_dirs) {
	std::filesystem::path fp(filepath);
	for (const auto& dir : script_dirs) {
		std::filesystem::path dp(dir);
		std::string fp_str = std::filesystem::absolute(fp).string();
		std::string dp_str = std::filesystem::absolute(dp).string();
		if (fp_str.size() > dp_str.size() &&
		    fp_str.compare(0, dp_str.size(), dp_str) == 0) {
			std::string relative = fp_str.substr(
				dp_str.size() + (dp_str.back() == '/' ||
				                 dp_str.back() == '\\' ? 0 : 1));
			for (auto& c : relative) {
				if (c == '/' || c == '\\') c = '.';
			}
			if (relative.size() > 4 &&
			    relative.compare(relative.size() - 4, 4, ".lua") == 0) {
				relative.resize(relative.size() - 4);
			}
			return relative;
		}
	}
	// Fallback: use stem.
	return fp.stem().string();
}

//=============================================================================
// ReloadFileCore — core reload logic without snapshot/restore
//=============================================================================

bool ScriptReloader::ReloadFileCore(lua_State* L, const std::string& filepath,
                                     const std::string& module_name) {
	auto* logger = GetLogger();

	// Remove only this module from package.loaded.
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

	// Load and execute the file.
	int ret = luaL_loadfile(L, filepath.c_str());
	if (ret != LUA_OK) {
		ENGINE_LOG_ERROR(logger,
		                 "ScriptReloader: load failed for [{}]: {}",
		                 filepath,
		                 lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	ret = lua_pcall(L, 0, 1, 0);
	if (ret != LUA_OK) {
		ENGINE_LOG_ERROR(logger,
		                 "ScriptReloader: execute failed for [{}]: {}",
		                 filepath,
		                 lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	// If the module returned a table, register it in package.loaded.
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
	lua_pop(L, 1);

	return true;
}

//=============================================================================
// ReloadFile — main-thread only, with per-file snapshot/restore
//=============================================================================

bool ScriptReloader::ReloadFile(const std::string& filepath) {
	if (!vm_) return false;

	auto* L = vm_->GetState();
	if (!L) return false;

	std::string module_name = ExtractModuleName(filepath, script_dirs_);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
		"ScriptReloader: reloading module [{}] from [{}]",
		module_name, filepath);

	// Per-file snapshot for rollback.
	SnapshotGlobals(L);
	SnapshotPackageLoaded(L, module_name);

	if (!ReloadFileCore(L, filepath, module_name)) {
		RestoreGlobals(L);
		RestorePackageLoaded(L, module_name);
		return false;
	}

	// Release the package.loaded snapshot — reload succeeded.
	if (package_loaded_snapshot_ref_ != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
		package_loaded_snapshot_ref_ = LUA_NOREF;
	}

	// Update debounce timestamp so the watcher doesn't re-trigger on this
	// file within the debounce window.
	file_reload_times_[filepath] = std::chrono::steady_clock::now();

	return true;
}

//=============================================================================
// ReloadAll — main-thread only, atomic rollback on failure
//=============================================================================

bool ScriptReloader::ReloadAll() {
	if (!vm_) return true;

	auto* L = vm_->GetState();
	if (!L) return true;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptReloader: reloading all scripts...");

	// Collect all .lua files first.
	struct FileEntry {
		std::string filepath;
		std::string module_name;
	};
	std::vector<FileEntry> files;
	for (const auto& dir : script_dirs_) {
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
		     it != std::filesystem::recursive_directory_iterator(); ++it) {
			if (ec) {
				ENGINE_LOG_WARN(logger,
					"ScriptReloader: iteration error in [{}]: {}",
					dir, ec.message());
				ec.clear();
				continue;
			}
			const auto& entry = *it;
			std::error_code fec;
			if (entry.is_regular_file(fec) &&
			    entry.path().extension() == ".lua") {
				std::string fp = entry.path().string();
				files.push_back({fp, ExtractModuleName(fp, script_dirs_)});
			}
			if (fec) fec.clear();
		}
	}

	if (files.empty()) {
		ENGINE_LOG_INFO(logger, "ScriptReloader: no scripts found to reload");
		return true;
	}

	// Snapshot initial state for atomic rollback.
	SnapshotGlobals(L);
	// Save the snapshot away so per-file ReloadFile calls don't overwrite it.
	auto initial_snapshot = std::move(global_snapshot_);

	bool all_ok = true;
	auto now = std::chrono::steady_clock::now();
	for (const auto& fe : files) {
		// Snapshot this file's package.loaded entry individually.
		SnapshotPackageLoaded(L, fe.module_name);
		if (!ReloadFileCore(L, fe.filepath, fe.module_name)) {
			ENGINE_LOG_ERROR(logger,
				"ScriptReloader: reload failed for [{}], rolling back all",
				fe.filepath);
			RestorePackageLoaded(L, fe.module_name);
			file_reload_times_[fe.filepath] = now;
			all_ok = false;
			break;
		}
		// Release this file's package.loaded snapshot on success.
		if (package_loaded_snapshot_ref_ != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
			package_loaded_snapshot_ref_ = LUA_NOREF;
		}
		file_reload_times_[fe.filepath] = now;
	}

	if (!all_ok) {
		// Restore initial global state, undoing all successful reloads.
		global_snapshot_ = std::move(initial_snapshot);
		RestoreGlobals(L);
		ENGINE_LOG_ERROR(logger,
			"ScriptReloader: reload-all rolled back to initial state");
		return false;
	}

	ENGINE_LOG_INFO(logger, "ScriptReloader: all [{}] scripts reloaded OK", files.size());
	return true;
}

void ScriptReloader::SetReloadCallback(ReloadCallback callback) {
	reload_callback_ = std::move(callback);
}

//=============================================================================
// Snapshot / Restore — typed, with package.loaded support
//=============================================================================

void ScriptReloader::SnapshotGlobal(lua_State* L, const char* key) {
	int t = lua_type(L, -1);
	SnapshotValue sv;

	switch (t) {
	case LUA_TBOOLEAN:
		sv.type = SnapshotValue::Type::Boolean;
		sv.bool_val = lua_toboolean(L, -1);
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, -1)) {
			sv.type = SnapshotValue::Type::Integer;
			sv.int_val = lua_tointeger(L, -1);
		} else {
			sv.type = SnapshotValue::Type::Number;
			sv.num_val = lua_tonumber(L, -1);
		}
		break;
	case LUA_TSTRING:
		sv.type = SnapshotValue::Type::String;
		{
			size_t len;
			const char* s = lua_tolstring(L, -1, &len);
			if (s) sv.str_val.assign(s, len);
		}
		break;
	case LUA_TNIL:
		sv.type = SnapshotValue::Type::Nil;
		break;
	default:
		// Tables, functions, userdata — not snapshottable.
		return;
	}

	global_snapshot_[key] = std::move(sv);
}

void ScriptReloader::SnapshotGlobals(lua_State* L) {
	global_snapshot_.clear();

	lua_pushglobaltable(L);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char* key = lua_tostring(L, -2);
			if (key) {
				SnapshotGlobal(L, key);
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);  // global table
}

void ScriptReloader::RestoreGlobals(lua_State* L) {
	for (const auto& [key, sv] : global_snapshot_) {
		switch (sv.type) {
		case SnapshotValue::Type::Nil:
			lua_pushnil(L);
			break;
		case SnapshotValue::Type::Boolean:
			lua_pushboolean(L, sv.bool_val ? 1 : 0);
			break;
		case SnapshotValue::Type::Integer:
			lua_pushinteger(L, sv.int_val);
			break;
		case SnapshotValue::Type::Number:
			lua_pushnumber(L, sv.num_val);
			break;
		case SnapshotValue::Type::String:
			lua_pushlstring(L, sv.str_val.data(), sv.str_val.size());
			break;
		}
		lua_setglobal(L, key.c_str());
	}
}

void ScriptReloader::SnapshotPackageLoaded(lua_State* L,
                                            const std::string& module_name) {
	// Release any previous snapshot.
	if (package_loaded_snapshot_ref_ != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
		package_loaded_snapshot_ref_ = LUA_NOREF;
	}
	package_loaded_snapshot_key_ = module_name;

	// Snapshot the current package.loaded[module_name] value.
	lua_getglobal(L, "package");       // ..., pkg
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "loaded");     // ..., pkg, loaded
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		return;
	}
	lua_getfield(L, -1, module_name.c_str());  // ..., pkg, loaded, value
	package_loaded_snapshot_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pop(L, 2);  // pop loaded, pkg
}

void ScriptReloader::RestorePackageLoaded(lua_State* L,
                                           const std::string& module_name) {
	if (package_loaded_snapshot_ref_ == LUA_NOREF) return;
	if (module_name != package_loaded_snapshot_key_) return;

	lua_getglobal(L, "package");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "loaded");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		return;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
	lua_setfield(L, -2, module_name.c_str());

	lua_pop(L, 2);  // pop loaded, pkg

	luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
	package_loaded_snapshot_ref_ = LUA_NOREF;
}

}  // namespace engine
