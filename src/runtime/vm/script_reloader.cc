#include "runtime/vm/script_reloader.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <string_view>
#include <unordered_set>

#include "runtime/core/log/log.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/vm/file_watcher.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {

namespace {

std::filesystem::path AbsoluteNormalPath(const std::filesystem::path& path) {
	std::error_code ec;
	auto absolute = std::filesystem::absolute(path, ec);
	if (ec) {
		return path.lexically_normal();
	}
	return absolute.lexically_normal();
}

bool IsUsableRelativePath(const std::filesystem::path& relative) {
	if (relative.empty() || relative.is_absolute()) return false;
	const auto rel_str = relative.generic_string();
	if (rel_str.empty() || rel_str == ".") return false;
	for (const auto& part : relative) {
		if (part == "..") return false;
	}
	return true;
}

bool IsPathUnderOrAtRoot(const std::filesystem::path& path,
                         const std::filesystem::path& root) {
	auto relative = path.lexically_relative(root);
	return relative.generic_string() == "." || IsUsableRelativePath(relative);
}

std::filesystem::path CommonRootForPaths(const std::vector<std::string>& paths) {
	if (paths.empty()) return {};

	std::filesystem::path common = AbsoluteNormalPath(paths.front());
	for (size_t i = 1; i < paths.size(); ++i) {
		const auto path = AbsoluteNormalPath(paths[i]);
		while (!common.empty() && !IsPathUnderOrAtRoot(path, common)) {
			auto parent = common.parent_path();
			if (parent == common) break;
			common = parent;
		}
	}
	return common;
}

std::vector<std::string> DeriveModuleRoots(const std::vector<std::string>& script_dirs) {
	std::vector<std::string> roots;
	for (const auto& dir : script_dirs) {
		if (!dir.empty()) {
			roots.push_back(dir);
		}
	}
	if (roots.size() <= 1) {
		return roots;
	}

	auto common = CommonRootForPaths(roots);
	if (!common.empty()) {
		return {common.string()};
	}
	return roots;
}

}  // namespace

ScriptReloader::ScriptReloader() = default;

ScriptReloader::~ScriptReloader() {
	Stop();
}

void ScriptReloader::SetTarget(ScriptVM* vm,
                                const std::vector<std::string>& script_dirs) {
	vm_ = vm;
	script_dirs_ = script_dirs;
	module_roots_ = DeriveModuleRoots(script_dirs_);
	if (vm_ && vm_->GetScriptRoots().empty()) {
		vm_->SetScriptRoots(module_roots_);
	}
	validator_.SetScriptDirs(script_dirs_);
}

void ScriptReloader::SetEventLoop(evpp::EventLoop* loop) {
	loop_ = loop;
}

void ScriptReloader::SetSandboxLevel(LuaSandboxLevel level) {
	sandbox_level_ = level;
	validator_.SetSandboxLevel(level);
}

void ScriptReloader::Start(int poll_interval_ms, int debounce_ms) {
	if (!vm_) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger,
			"ScriptReloader: SetTarget() must be called before Start()");
		return;
	}

#if !ENGINE_FILE_WATCHER_ENABLED
	Stop();
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: file watching disabled on [{}]",
	                ENGINE_PLATFORM_NAME);
	return;
#else
	// Stop any existing watcher and reset state for a clean restart.
	Stop();
	stopped_.store(false, std::memory_order_release);
	generation_.fetch_add(1, std::memory_order_acq_rel);

	// Clean up Reference-type snapshot entries before clearing.
	{
		auto* L = vm_->GetState();
		if (L) ClearSnapshot(L);
		{
			std::lock_guard<std::mutex> lock(file_reload_mutex_);
			file_reload_times_.clear();
		}
	}
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
#endif
}

void ScriptReloader::Stop() {
	stopped_.store(true, std::memory_order_release);
	generation_.fetch_add(1, std::memory_order_acq_rel);
	if (watcher_) {
		watcher_->Stop();
		watcher_.reset();
	}
	if (loop_ && loop_->IsRunning() && !loop_->IsInLoopThread()) {
		struct DrainState {
			std::mutex mutex;
			std::condition_variable cv;
			bool drained = false;
		};
		auto state = std::make_shared<DrainState>();
		loop_->RunInLoop([state]() {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->drained = true;
			state->cv.notify_one();
		});
		std::unique_lock<std::mutex> lock(state->mutex);
		state->cv.wait_for(lock, std::chrono::seconds(2), [&]() { return state->drained; });
	}
}

// OnFilesChanged — called on watcher thread

void ScriptReloader::OnFilesChanged(const std::vector<std::string>& files) {
	auto* logger = GetLogger();

	// Per-file debounce: filter out files changed within the debounce window.
	auto now = std::chrono::steady_clock::now();
	std::vector<std::string> ready_files;
	{
		std::lock_guard<std::mutex> lock(file_reload_mutex_);
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
			ready_files.push_back(file);
		}
	}

	if (ready_files.empty()) return;

	ENGINE_LOG_INFO(logger,
	                "ScriptReloader: [{}] file(s) changed after debounce",
	                ready_files.size());

	// Validate each changed file in a sandbox VM (thread-safe).
	// Debounce timestamps are set only after successful validation so that
	// a validation failure does not block a subsequent change within the
	// debounce window.
	std::vector<std::string> valid_files;
	std::vector<std::string> failed_files;
	for (const auto& file : ready_files) {
		if (ValidateScript(file)) {
			valid_files.push_back(file);
			std::lock_guard<std::mutex> lock(file_reload_mutex_);
			file_reload_times_[file] = now;
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
			const uint64_t generation = generation_.load(std::memory_order_acquire);
			loop_->RunInLoop([this, generation, files = std::move(valid_files)]() {
				if (!IsDispatchActive(generation)) return;
				ProcessReloadList(files);
			});
		}
		if (!failed_files.empty()) {
			const uint64_t generation = generation_.load(std::memory_order_acquire);
			loop_->RunInLoop([this, generation, files = std::move(failed_files)]() {
				if (!IsDispatchActive(generation)) return;
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

bool ScriptReloader::IsDispatchActive(uint64_t generation) const {
	return !stopped_.load(std::memory_order_acquire) &&
		   generation_.load(std::memory_order_acquire) == generation;
}

// ProcessReloadList — called on main thread (via EventLoop or manual)

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

// ValidateScript runs the script in a dedicated validator VM before the
// target hot-reload VM is touched.
namespace {

std::string_view ValidationStatusName(LuaScriptValidationResult::Status status) {
	using Status = LuaScriptValidationResult::Status;
	switch (status) {
	case Status::Ok:
		return "ok";
	case Status::FileNotFound:
		return "file not found";
	case Status::VmCreateFailed:
		return "validation VM create failed";
	case Status::CompileError:
		return "compile error";
	case Status::RuntimeError:
		return "runtime error";
	}
	return "unknown";
}

}  // namespace

bool ScriptReloader::ValidateScript(const std::string& filepath) {
	auto* logger = GetLogger();

	auto result = validator_.ValidateFile(filepath);
	if (result.ok()) {
		ENGINE_LOG_INFO(logger,
		                "ScriptReloader: validation passed [{}]",
		                filepath);
		return true;
	}

	ENGINE_LOG_WARN(logger,
	                "ScriptReloader: validation failed [{}] ({}): {}",
	                filepath,
	                ValidationStatusName(result.status),
	                result.error.empty() ? "no details" : result.error);
	return false;
}

// Module name extraction helper

std::vector<std::string> ScriptReloader::ResolveModuleNames(
	const std::string& filepath) const {
	std::vector<std::string> roots;
	if (vm_ && !vm_->GetScriptRoots().empty()) {
		roots = vm_->GetScriptRoots();
	} else {
		roots = module_roots_;
	}

	std::vector<std::string> names;
	std::string primary = ScriptVM::BuildDefaultModuleNameForFile(filepath, roots);
	if (primary.empty()) {
		primary = std::filesystem::path(filepath).stem().string();
	}
	names.push_back(primary);

	std::string dotted = ScriptVM::BuildModuleNameForFile(filepath, roots, '.');
	if (!dotted.empty() && dotted != primary) {
		names.push_back(dotted);
	}
	return names;
}

// ReloadFileCore — core reload logic without snapshot/restore

bool ScriptReloader::ReloadFileCore(lua_State* L, const std::string& filepath,
                                     const std::vector<std::string>& module_names) {
	if (module_names.empty()) return false;
	const std::string& module_name = module_names.front();
	auto* logger = GetLogger();
	int base_top = lua_gettop(L);

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
		const char* msg = lua_tostring(L, -1);
		ENGINE_LOG_ERROR(logger,
		                 "ScriptReloader: load failed for [{}]: {}",
		                 filepath,
		                 msg ? msg : "unknown Lua load error");
		lua_settop(L, base_top);
		return false;
	}

	int msgh = 0;
	{
		msgh = PushLuaErrorHandlerForCall(L, 0);
		ret = lua_pcall(L, 0, 1, msgh);
	}
	if (ret != LUA_OK) {
		const char* msg = lua_tostring(L, -1);
		ENGINE_LOG_ERROR(logger,
		                 "ScriptReloader: execute failed for [{}]: {}",
		                 filepath,
		                 msg ? msg : "unknown Lua runtime error");
		lua_settop(L, base_top);
		return false;
	}
	lua_remove(L, msgh);

	// Register the returned module value in package.loaded.  A nil return
	// follows require() semantics and records true.
	lua_getglobal(L, "package");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "loaded");
		if (lua_istable(L, -1)) {
			for (const auto& name : module_names) {
				if (name.empty()) continue;
				if (lua_isnil(L, -3)) {
					lua_pushboolean(L, 1);
				} else {
					lua_pushvalue(L, -3);
				}
				lua_setfield(L, -2, name.c_str());
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_settop(L, base_top);

	return true;
}

// ReloadFile — main-thread only, with per-file snapshot/restore

bool ScriptReloader::ReloadFile(const std::string& filepath) {
	if (!vm_) return false;

	auto* L = vm_->GetState();
	if (!L) return false;

	if (!ValidateScript(filepath)) {
		return false;
	}

	auto module_names = ResolveModuleNames(filepath);
	const std::string& module_name = module_names.front();

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
		"ScriptReloader: reloading module [{}] from [{}]",
		module_name, filepath);

	// Per-file snapshot for rollback.
	SnapshotGlobals(L);
	SnapshotPackageLoaded(L, module_name);

	if (!ReloadFileCore(L, filepath, module_names)) {
		RestoreGlobals(L);
		RestorePackageLoaded(L, module_name);
		ClearSnapshot(L);
		return false;
	}

	// Release the package.loaded snapshot — reload succeeded.
	if (package_loaded_snapshot_ref_ != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
		package_loaded_snapshot_ref_ = LUA_NOREF;
	}
	ClearSnapshot(L);

	// Update debounce timestamp so the watcher doesn't re-trigger on this
	// file within the debounce window.
	{
		std::lock_guard<std::mutex> lock(file_reload_mutex_);
		file_reload_times_[filepath] = std::chrono::steady_clock::now();
	}

	return true;
}

// ReloadAll — main-thread only, atomic rollback on failure

bool ScriptReloader::ReloadAll() {
	if (!vm_) return true;

	auto* L = vm_->GetState();
	if (!L) return true;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptReloader: reloading all scripts...");

	// Collect all .lua files first.
	struct FileEntry {
		std::string filepath;
		std::vector<std::string> module_names;
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
			const auto ext = entry.path().extension().string();
			if (entry.is_regular_file(fec) && (ext == ".lua" || ext == ".LUA")) {
				std::string fp = entry.path().string();
				files.push_back({fp, ResolveModuleNames(fp)});
			}
			if (fec) fec.clear();
		}
	}

	if (files.empty()) {
		ENGINE_LOG_INFO(logger, "ScriptReloader: no scripts found to reload");
		return true;
	}

	for (const auto& fe : files) {
		if (!ValidateScript(fe.filepath)) {
			ENGINE_LOG_ERROR(logger,
				"ScriptReloader: reload-all validation failed for [{}], aborting",
				fe.filepath);
			return false;
		}
	}

	// Snapshot initial state for atomic rollback.
	SnapshotGlobals(L);
	auto initial_snapshot = std::move(global_snapshot_);

	// Record all pre-existing global keys (including non-snapshottable
	// types like functions and tables) so that after rollback we can
	// nil out any globals that were created by partially-reloaded files.
	std::unordered_set<std::string> initial_keys;
	{
		lua_pushglobaltable(L);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (lua_type(L, -2) == LUA_TSTRING) {
				const char* key = lua_tostring(L, -2);
				if (key) initial_keys.insert(key);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}

	// Snapshot the entire package.loaded table for full restoration on
	// rollback. This covers all previously-succeeded files, unlike the
	// per-file SnapshotPackageLoaded which only handles one module.
	int pkg_loaded_snapshot = LUA_NOREF;
	{
		lua_getglobal(L, "package");
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "loaded");
			if (lua_istable(L, -1)) {
				pkg_loaded_snapshot = luaL_ref(L, LUA_REGISTRYINDEX);
			} else {
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);  // pop package or nil
	}

	bool all_ok = true;
	auto now = std::chrono::steady_clock::now();
	for (const auto& fe : files) {
		if (fe.module_names.empty()) continue;
		const std::string& module_name = fe.module_names.front();
		// Snapshot this file's package.loaded entry individually.
		SnapshotPackageLoaded(L, module_name);
		if (!ReloadFileCore(L, fe.filepath, fe.module_names)) {
			ENGINE_LOG_ERROR(logger,
				"ScriptReloader: reload failed for [{}], rolling back all",
				fe.filepath);
			RestorePackageLoaded(L, module_name);
			{
				std::lock_guard<std::mutex> lock(file_reload_mutex_);
				file_reload_times_[fe.filepath] = now;
			}
			all_ok = false;
			break;
		}
		// Release this file's package.loaded snapshot on success.
		if (package_loaded_snapshot_ref_ != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
			package_loaded_snapshot_ref_ = LUA_NOREF;
		}
		{
			std::lock_guard<std::mutex> lock(file_reload_mutex_);
			file_reload_times_[fe.filepath] = now;
		}
	}

	if (!all_ok) {
		// Restore the entire package.loaded table to its pre-reload state,
		// undoing changes from previously-succeeded files.
		if (pkg_loaded_snapshot != LUA_NOREF) {
			lua_getglobal(L, "package");
			if (lua_istable(L, -1)) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, pkg_loaded_snapshot);
				lua_setfield(L, -2, "loaded");
			}
			lua_pop(L, 1);
			luaL_unref(L, LUA_REGISTRYINDEX, pkg_loaded_snapshot);
		}

		// Restore initial global state, undoing all successful reloads.
		global_snapshot_ = std::move(initial_snapshot);
		RestoreGlobals(L);

		// Nil out globals that were created during the reload (keys that
		// exist now but were not present before the reload).
		{
			std::vector<std::string> leaked_keys;
			lua_pushglobaltable(L);
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				if (lua_type(L, -2) == LUA_TSTRING) {
					const char* key = lua_tostring(L, -2);
					if (key && initial_keys.find(key) == initial_keys.end()) {
						leaked_keys.push_back(key);
					}
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
			for (const auto& key : leaked_keys) {
				lua_pushnil(L);
				lua_setglobal(L, key.c_str());
			}
			if (!leaked_keys.empty()) {
				ENGINE_LOG_WARN(logger,
					"ScriptReloader: rollback cleaned up [{}] leaked global(s)",
					leaked_keys.size());
			}
		}

		ENGINE_LOG_ERROR(logger,
			"ScriptReloader: reload-all rolled back to initial state");
		ClearSnapshot(L);
		return false;
	}

	// Clean up initial snapshot references on the success path.
	// The rollback path keeps them (RestoreGlobals uses them), but on
	// success they are no longer needed and must be freed.
	if (pkg_loaded_snapshot != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, pkg_loaded_snapshot);
	}
	for (auto& [key, sv] : initial_snapshot) {
		if (sv.type == SnapshotValue::Type::Reference && sv.ref_val != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, sv.ref_val);
		}
	}

	ENGINE_LOG_INFO(logger, "ScriptReloader: all [{}] scripts reloaded OK", files.size());
	return true;
}

void ScriptReloader::SetReloadCallback(ReloadCallback callback) {
	reload_callback_ = std::move(callback);
}

// Snapshot / Restore — typed, with package.loaded support

void ScriptReloader::ClearSnapshot(lua_State* L) {
	for (auto& [key, sv] : global_snapshot_) {
		if (sv.type == SnapshotValue::Type::Reference && sv.ref_val != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, sv.ref_val);
			sv.ref_val = LUA_NOREF;
		}
	}
	global_snapshot_.clear();
}

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
		// Tables, functions, userdata — store a registry reference.
		// luaL_ref pops the value, so push a copy first.
		lua_pushvalue(L, -1);
		sv.type = SnapshotValue::Type::Reference;
		sv.ref_val = luaL_ref(L, LUA_REGISTRYINDEX);
		break;
	}

	global_snapshot_[key] = std::move(sv);
}

void ScriptReloader::SnapshotGlobals(lua_State* L) {
	ClearSnapshot(L);

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
		case SnapshotValue::Type::Reference:
			if (sv.ref_val != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, sv.ref_val);
			} else {
				lua_pushnil(L);
			}
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
		luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
		package_loaded_snapshot_ref_ = LUA_NOREF;
		return;
	}
	lua_getfield(L, -1, "loaded");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
		package_loaded_snapshot_ref_ = LUA_NOREF;
		return;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
	lua_setfield(L, -2, module_name.c_str());

	lua_pop(L, 2);  // pop loaded, pkg

	luaL_unref(L, LUA_REGISTRYINDEX, package_loaded_snapshot_ref_);
	package_loaded_snapshot_ref_ = LUA_NOREF;
}

}  // namespace engine
