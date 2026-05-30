#include "runtime/vm/file_watcher.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#include "runtime/core/log/log.h"

namespace engine {

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
		         entry.path, ec);
		     it != std::filesystem::recursive_directory_iterator();
		     ++it) {
			if (ec) { ec.clear(); continue; }
			const auto& de = *it;
			if (!de.is_regular_file(ec)) continue;
			if (ec) { ec.clear(); continue; }
			auto ext = de.path().extension().string();
			if (!entry.extension.empty() && ext != entry.extension) continue;
			auto path_str = de.path().string();
			known_files_.insert(path_str);
			auto ftime = std::filesystem::last_write_time(de, ec);
			if (!ec) {
				file_times_[path_str] = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
			} else {
				ec.clear();
			}
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

// Convert filesystem file_time_type to system_clock time_point.
// Uses the approach: system_clock::now() - (file_clock::now() - ftime)
// = system_clock::now() - age_of_file. This works correctly even when
// the clocks have different epochs.
// Clamp age to zero so files with timestamps in the future
// do not produce a time_point far in the future.
static std::chrono::system_clock::time_point ToSystemClock(
    std::filesystem::file_time_type ftime) {
	auto file_now = std::filesystem::file_time_type::clock::now();
	auto sys_now = std::chrono::system_clock::now();
	auto age = file_now - ftime;
	if (age.count() < 0) {
		age = decltype(age)::zero();
	}
	return sys_now - std::chrono::duration_cast<
	                    std::chrono::system_clock::duration>(age);
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
		         entry.path, ec);
		     it != std::filesystem::recursive_directory_iterator();
		     ++it) {
			if (ec) {
				ec.clear();
				continue;
			}

			const auto& dir_entry = *it;
			if (!dir_entry.is_regular_file(ec)) continue;
			if (ec) { ec.clear(); continue; }

			auto ext = dir_entry.path().extension().string();
			if (!entry.extension.empty() && ext != entry.extension) {
				continue;
			}

			auto path_str = dir_entry.path().string();
			seen.insert(path_str);

			auto ftime = std::filesystem::last_write_time(dir_entry, ec);
			if (ec) { ec.clear(); continue; }

			auto sctp = ToSystemClock(ftime);

			// New file detection: report files not previously known.
			if (known_files_.find(path_str) == known_files_.end()) {
				known_files_.insert(path_str);
				file_times_[path_str] = sctp;
				changed.push_back(path_str);
				continue;
			}

			auto it_mtime = file_times_.find(path_str);
			if (it_mtime == file_times_.end()) {
				file_times_[path_str] = sctp;
			} else if (sctp > it_mtime->second) {
				it_mtime->second = sctp;
				changed.push_back(path_str);
			}
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

}  // namespace engine
