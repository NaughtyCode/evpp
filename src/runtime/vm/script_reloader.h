#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

class FileWatcher;
class ScriptVM;

//=============================================================================
// ScriptReloader — hot-reload lifecycle manager
//
// Coordinates the hot-reload workflow:
//   1. Detect file changes (via FileWatcher, with debounce)
//   2. Validate changed scripts in an isolated sandbox VM
//   3. Apply reload by clearing caches and re-importing
//   4. Rollback on validation failure
//=============================================================================

class ENGINE_API ScriptReloader {
	public:
	ScriptReloader();
	~ScriptReloader();

	ScriptReloader(const ScriptReloader&) = delete;
	ScriptReloader& operator=(const ScriptReloader&) = delete;

	// Set the script directories to watch and the target ScriptVM.
	void SetTarget(ScriptVM* vm, const std::vector<std::string>& script_dirs);

	// Start file watching and hot-reload.
	void Start(int poll_interval_ms = 1000, int debounce_ms = 300);

	// Stop file watching.
	void Stop();

	// Manual reload of a single file. Returns true on success.
	bool ReloadFile(const std::string& filepath);

	// Manual reload of all watched scripts.
	bool ReloadAll();

	// Callback invoked after each reload attempt.
	using ReloadCallback =
		std::function<void(const std::string& filepath, bool success)>;
	void SetReloadCallback(ReloadCallback callback);

	private:
	// Validate a script by loading it in a sandbox VM.
	// Returns true if the script compiles without errors.
	bool ValidateScript(const std::string& filepath);

	// Called by FileWatcher when changes are detected.
	void OnFilesChanged(const std::vector<std::string>& files);

	// Snapshot global variable names for potential rollback.
	void SnapshotGlobals(lua_State* L);

	// Restore global state from snapshot (best-effort).
	void RestoreGlobals(lua_State* L);

	ScriptVM* vm_ = nullptr;
	std::vector<std::string> script_dirs_;
	std::unique_ptr<FileWatcher> watcher_;

	ReloadCallback reload_callback_;

	int debounce_ms_ = 300;
	std::chrono::steady_clock::time_point last_reload_time_;

	// Rollback state: global name → serialized value (best-effort)
	std::unordered_map<std::string, std::string> global_snapshot_;
};

}  // namespace engine
