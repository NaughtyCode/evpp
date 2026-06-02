#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/vm/sandbox.h"
#include "runtime/vm/script_validator.h"

#include "lua.h"
#include "lauxlib.h"

struct lua_State;

namespace evpp {
class EventLoop;
}

namespace engine {

class FileWatcher;
class ScriptVM;

// ScriptReloader — hot-reload lifecycle manager
//
// Coordinates the hot-reload workflow:
//   1. Detect file changes (via FileWatcher, with per-file debounce)
//   2. Validate changed scripts in an isolated sandbox VM
//   3. Dispatch actual reload to the main thread via EventLoop::RunInLoop
//   4. Clear only the target module's cache entry (not entire package.loaded)
//   5. Restore previous state on reload failure
//
// Thread safety:
//   - ValidateScript runs on the watcher thread (uses sandbox VM)
//   - ReloadFile runs on the main thread (dispatched via EventLoop)
//   - SetEventLoop must be called before Start

// Typed snapshot value — preserves Lua type across snapshot/restore.
struct SnapshotValue {
	enum class Type { Nil, Boolean, Integer, Number, String, Reference };

	Type type = Type::Nil;
	bool bool_val = false;
	lua_Integer int_val = 0;
	lua_Number num_val = 0.0;
	std::string str_val;
	int ref_val = LUA_NOREF;  // registry reference for functions/tables/userdata
};

class CLOUD_ENGINE_API ScriptReloader {
	public:
	ScriptReloader();
	~ScriptReloader();

	ScriptReloader(const ScriptReloader&) = delete;
	ScriptReloader& operator=(const ScriptReloader&) = delete;

	// Set the target ScriptVM and script directories to watch.
	void SetTarget(ScriptVM* vm, const std::vector<std::string>& script_dirs);

	// Set the EventLoop for main-thread dispatch.
	// Required for thread-safe reload. Call before Start().
	void SetEventLoop(evpp::EventLoop* loop);

	// Set the sandbox level used when validating scripts.
	// Must match the target ScriptVM's sandbox level so that scripts
	// using io/os (allowed in Server/Full levels) can pass validation.
	// Default: Strict. Call before Start().
	void SetSandboxLevel(LuaSandboxLevel level);

	// Start file watching and hot-reload.
	// poll_interval_ms: how often to scan for file changes.
	// debounce_ms: per-file minimum interval between reloads.
	void Start(int poll_interval_ms = 1000, int debounce_ms = 300);

	// Stop file watching.
	void Stop();

	// Process pending reloads. Call from the main thread if not using
	// an EventLoop (e.g., in headless/test environments).
	void ProcessPendingReloads();

	// Manual reload of a single file. Returns true on success.
	// Must be called from the main thread.
	bool ReloadFile(const std::string& filepath);

	// Manual reload of all watched scripts. Returns true on success.
	// Must be called from the main thread.
	bool ReloadAll();

	// Callback invoked after each reload attempt (on the main thread).
	using ReloadCallback =
		std::function<void(const std::string& filepath, bool success)>;
	void SetReloadCallback(ReloadCallback callback);

	private:
	// Validate a script by loading it in a sandbox VM.
	// Thread-safe: creates its own isolated lua_State.
	bool ValidateScript(const std::string& filepath);

	// Called by FileWatcher when changes are detected (watcher thread).
	void OnFilesChanged(const std::vector<std::string>& files);

	// Core reload without snapshot/restore (caller manages rollback).
	// module_name must be the dotted package.loaded key.
	bool ReloadFileCore(lua_State* L, const std::string& filepath,
	                    const std::string& module_name);

	// Process the validated reload list (main thread).
	void ProcessReloadList(const std::vector<std::string>& files);
	bool IsDispatchActive(uint64_t generation) const;

	// Snapshot a single global variable for potential rollback.
	void SnapshotGlobal(lua_State* L, const char* key);

	// Snapshot all tracked globals before reload.
	void SnapshotGlobals(lua_State* L);

	// Restore global state from snapshot (best-effort).
	void RestoreGlobals(lua_State* L);

	// Save and restore package.loaded for the target module.
	void SnapshotPackageLoaded(lua_State* L, const std::string& module_name);
	void RestorePackageLoaded(lua_State* L, const std::string& module_name);

	// Unref all Reference-type entries in global_snapshot_, then clear it.
	void ClearSnapshot(lua_State* L);

	ScriptVM* vm_ = nullptr;
	evpp::EventLoop* loop_ = nullptr;
	std::vector<std::string> script_dirs_;
	std::unique_ptr<FileWatcher> watcher_;
	LuaSandboxLevel sandbox_level_ = LuaSandboxLevel::Strict;
	LuaScriptValidator validator_;
	std::atomic<uint64_t> generation_{0};
	std::atomic<bool> stopped_{true};

	ReloadCallback reload_callback_;

	int debounce_ms_ = 300;

	// Per-file last reload time for debounce.
	// Protected by file_reload_mutex_ — accessed from watcher thread
	// (OnFilesChanged) and main thread (ReloadFile, ReloadAll).
	std::unordered_map<std::string, std::chrono::steady_clock::time_point>
		file_reload_times_;
	mutable std::mutex file_reload_mutex_;

	// Rollback state: global name → typed value
	std::unordered_map<std::string, SnapshotValue> global_snapshot_;

	// package.loaded snapshot for rollback: module_name → serialized value
	std::string package_loaded_snapshot_key_;
	int package_loaded_snapshot_ref_ = LUA_NOREF;

	// Pending queues (written by watcher thread, read by main thread).
	std::vector<std::string> pending_reloads_;
	std::vector<std::string> pending_failures_;
	std::mutex pending_mutex_;
};

}  // namespace engine
