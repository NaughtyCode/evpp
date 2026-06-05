#include "runtime/vm/file_watcher.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <thread>
#include <utility>

#include "runtime/core/log/log.h"

namespace engine {

namespace {

bool ExtensionMatches(std::string_view actual, std::string_view expected) {
	if (expected.empty() || actual == expected) return true;
	return expected == ".lua" && actual == ".LUA";
}

}  // namespace

#if ENGINE_FILE_WATCHER_ENABLED

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
	Stop();
}

void FileWatcher::WatchDirectory(const std::string& path,
                                  const std::string& extension) {
	watch_entries_.push_back({path, extension});

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger,
		                "FileWatcher: watching [{}] for [{}] files",
		                path, extension);
	}
}

void FileWatcher::SetChangeCallback(ChangeCallback callback) {
	callback_ = std::move(callback);
}

void FileWatcher::Start(int poll_interval_ms) {
	if (running_.load(std::memory_order_acquire)) return;
	if (poll_interval_ms < 1) poll_interval_ms = 1;

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger,
		                "FileWatcher: starting, watching [{}] dir(s), "
		                "interval=[{}ms]",
		                watch_entries_.size(),
		                poll_interval_ms);
	}

	running_.store(true, std::memory_order_release);
	try {
		thread_ = std::make_unique<std::thread>(
			[this, poll_interval_ms]() { WatchLoop(poll_interval_ms); });
	} catch (...) {
		running_.store(false, std::memory_order_release);
		thread_.reset();
		throw;
	}
}

void FileWatcher::PrimeKnownFiles() {
	std::error_code ec;
	for (const auto& entry : watch_entries_) {
		if (!std::filesystem::exists(entry.path, ec)) {
			if (ec) ec.clear();
			continue;
		}
		for (auto it = std::filesystem::recursive_directory_iterator(
				 entry.path,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec),
				  end = std::filesystem::recursive_directory_iterator();
			 it != end;) {
			if (ec) {
				ec.clear();
				break;
			}
			const auto& de = *it;
			const bool regular_file = de.is_regular_file(ec);
			if (ec) {
				ec.clear();
				it.increment(ec);
				continue;
			}
			if (!regular_file) {
				it.increment(ec);
				continue;
			}
			auto ext = de.path().extension().string();
			if (!ExtensionMatches(ext, entry.extension)) {
				it.increment(ec);
				continue;
			}
			auto path_str = de.path().string();
			known_files_.insert(path_str);
			auto ftime = std::filesystem::last_write_time(de, ec);
			if (!ec) {
				file_times_[path_str] = ftime;
			} else {
				ec.clear();
			}
			it.increment(ec);
		}
	}
}

void FileWatcher::Stop() {
	if (!running_.load(std::memory_order_acquire) && !thread_) return;

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "FileWatcher: stopping...");
	}

	running_.store(false, std::memory_order_release);
	if (thread_ && thread_->joinable()) {
		thread_->join();
	}
	thread_.reset();

	if (logger) {
		ENGINE_LOG_INFO(logger, "FileWatcher: stopped");
	}
}

void FileWatcher::WatchLoop(int poll_interval_ms) {
	SetCurrentThreadName("FileWatcher");

	while (running_.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));

		if (!running_.load(std::memory_order_acquire)) break;

		auto changed = ScanChanges();
		if (!changed.empty() && callback_) {
			try {
				callback_(changed);
			} catch (const std::exception& e) {
				auto* logger = GetLogger();
				if (logger) {
					ENGINE_LOG_ERROR(logger,
						"FileWatcher: callback exception: {}", e.what());
				}
			} catch (...) {
				auto* logger = GetLogger();
				if (logger) {
					ENGINE_LOG_ERROR(logger,
						"FileWatcher: callback exception: unknown");
				}
			}
		}
	}
}

std::vector<std::string> FileWatcher::ScanChanges() {
	std::vector<std::string> changed;
	std::error_code ec;

	// Track which known files were seen this scan to detect deletions.
	std::unordered_set<std::string> seen;

	for (const auto& entry : watch_entries_) {
		if (!std::filesystem::exists(entry.path, ec)) {
			if (ec) ec.clear();
			continue;
		}

		for (auto it = std::filesystem::recursive_directory_iterator(
				 entry.path,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec),
				  end = std::filesystem::recursive_directory_iterator();
			 it != end;) {
			if (ec) {
				ec.clear();
				break;
			}

			const auto& dir_entry = *it;
			const bool regular_file = dir_entry.is_regular_file(ec);
			if (ec) {
				ec.clear();
				it.increment(ec);
				continue;
			}
			if (!regular_file) {
				it.increment(ec);
				continue;
			}

			auto ext = dir_entry.path().extension().string();
			if (!ExtensionMatches(ext, entry.extension)) {
				it.increment(ec);
				continue;
			}

			auto path_str = dir_entry.path().string();
			seen.insert(path_str);

			auto ftime = std::filesystem::last_write_time(dir_entry, ec);
			if (ec) {
				ec.clear();
				it.increment(ec);
				continue;
			}

			// New file detection: report files not previously known.
			if (known_files_.find(path_str) == known_files_.end()) {
				known_files_.insert(path_str);
				file_times_[path_str] = ftime;
				changed.push_back(path_str);
				it.increment(ec);
				continue;
			}

			auto it_mtime = file_times_.find(path_str);
			if (it_mtime == file_times_.end()) {
				file_times_[path_str] = ftime;
			} else if (ftime > it_mtime->second) {
				it_mtime->second = ftime;
				changed.push_back(path_str);
			}
			it.increment(ec);
		}
	}

	// Clean up stale entries for files that no longer exist.
	for (auto it = known_files_.begin(); it != known_files_.end(); ) {
		if (seen.find(*it) == seen.end()) {
			file_times_.erase(*it);
			it = known_files_.erase(it);
		} else {
			++it;
		}
	}

	return changed;
}

#else

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
	Stop();
}

void FileWatcher::WatchDirectory(const std::string& path,
                                  const std::string& extension) {
	watch_entries_.push_back({path, extension});

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger,
		                "FileWatcher: disabled on [{}], ignoring watch dir [{}]",
		                ENGINE_PLATFORM_NAME,
		                path);
	}
}

void FileWatcher::SetChangeCallback(ChangeCallback callback) {
	callback_ = std::move(callback);
}

void FileWatcher::Start(int) {
	running_.store(false, std::memory_order_release);

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger,
		                "FileWatcher: disabled on [{}], not starting",
		                ENGINE_PLATFORM_NAME);
	}
}

void FileWatcher::PrimeKnownFiles() {
}

void FileWatcher::Stop() {
	running_.store(false, std::memory_order_release);
	thread_.reset();
}

void FileWatcher::WatchLoop(int) {
}

std::vector<std::string> FileWatcher::ScanChanges() {
	return {};
}

#endif

}  // namespace engine
