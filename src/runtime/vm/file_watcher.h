#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/core/engine_api.h"

namespace engine {

//=============================================================================
// FileWatcher — cross-platform file change monitor
//
// Monitors directories for changes to files matching configured extensions.
// Uses polling with filesystem timestamps as the cross-platform fallback,
// with optional platform-specific backends (inotify/ReadDirectoryChangesW).
//
// Each watched directory can have its own extension filter. New files that
// appear in a watched directory are immediately reported as changes.
//
// Callbacks are invoked on the watcher's own thread. The callback
// implementation is responsible for thread-safe dispatch (e.g., RunInLoop
// for Lua state access).
//=============================================================================

class ENGINE_API FileWatcher {
	public:
	using ChangeCallback =
		std::function<void(const std::vector<std::string>& changed_files)>;

	FileWatcher();
	~FileWatcher();

	FileWatcher(const FileWatcher&) = delete;
	FileWatcher& operator=(const FileWatcher&) = delete;

	// Watch a directory recursively for files with the given extension.
	// extension should include the dot, e.g. ".lua".
	// Multiple directories may be watched with different extensions.
	// Must be called before Start(). Not thread-safe with ScanChanges.
	void WatchDirectory(const std::string& path,
	                    const std::string& extension = ".lua");

	// Set the callback invoked when changes are detected.
	void SetChangeCallback(ChangeCallback callback);

	// Start monitoring. Poll interval in milliseconds (default 1000ms).
	void Start(int poll_interval_ms = 1000);

	// Stop monitoring and join the watcher thread.
	void Stop();

	// Whether the watcher is currently running.
	bool IsRunning() const {
		return running_.load(std::memory_order_acquire);
	}

	private:
	// Per-directory watch configuration.
	struct WatchEntry {
		std::string path;
		std::string extension;
	};

	// Scan watched directories and return changed files.
	std::vector<std::string> ScanChanges();

	// Watcher thread function.
	void WatchLoop(int poll_interval_ms);

	std::vector<WatchEntry> watch_entries_;
	ChangeCallback callback_;

	// Last recorded modification time per file path.
	std::unordered_map<std::string,
	                   std::chrono::system_clock::time_point> file_times_;
	// Known file set for new-file detection.
	std::unordered_set<std::string> known_files_;

	std::unique_ptr<std::thread> thread_;
	std::atomic<bool> running_{false};
};

}  // namespace engine
