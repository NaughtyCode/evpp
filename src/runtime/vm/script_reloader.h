#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "runtime/core/engine_api.h"

struct lua_State;

namespace evpp {
class EventLoop;
}

namespace engine {

class FileWatcher;
class ScriptVM;

//=============================================================================
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
//=============================================================================

// Typed snapshot value — preserves Lua type across snapshot/restore.
struct SnapshotValue {
	enum class Type { Nil, Boolean, Integer, Number, String };

	Type type = Type::Nil;
	bool bool_val = false;
	lua_Integer int_val = 0;
	lua_Number num_val = 0.0;
	std::string str_val;
};

class ENGINE_API ScriptReloader {
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

	// Process the validated reload list (main thread).
	void ProcessReloadList(const std::vector<std::string>& files);

	// Snapshot a single global variable for potential rollback.
	void SnapshotGlobal(lua_State* L, const char* key);

	// Snapshot all tracked globals before reload.
	void SnapshotGlobals(lua_State* L);

	// Restore global state from snapshot (best-effort).
	void RestoreGlobals(lua_State* L);

	// Save and restore package.loaded for the target module.
	void SnapshotPackageLoaded(lua_State* L, const std::string& module_name);
	void RestorePackageLoaded(lua_State* L, const std::string& module_name);

	ScriptVM* vm_ = nullptr;
	evpp::EventLoop* loop_ = nullptr;
	std::vector<std::string> script_dirs_;
	std::unique_ptr<FileWatcher> watcher_;

	ReloadCallback reload_callback_;

	int debounce_ms_ = 300;

	// Per-file last reload time for debounce.
	std::unordered_map<std::string, std::chrono::steady_clock::time_point>
		file_reload_times_;

	// Rollback state: global name → typed value
	std::unordered_map<std::string, SnapshotValue> global_snapshot_;

	// package.loaded snapshot for rollback: module_name → serialized value
	std::string package_loaded_snapshot_key_;
	int package_loaded_snapshot_ref_ = LUA_NOREF;

	// Pending reload queue (written by watcher thread, read by main thread).
	std::vector<std::string> pending_reloads_;
	std::mutex pending_mutex_;
};

}  // namespace engine
