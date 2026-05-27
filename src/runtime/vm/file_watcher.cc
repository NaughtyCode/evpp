#include "runtime/vm/file_watcher.h"

#include <algorithm>
#include <filesystem>

#include "runtime/core/log/log.h"

namespace engine {

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
	Stop();
}

void FileWatcher::WatchDirectory(const std::string& path,
                                  const std::string& extension) {
	watch_dirs_.push_back(path);
	extension_ = extension;
}

void FileWatcher::SetChangeCallback(ChangeCallback callback) {
	callback_ = std::move(callback);
}

void FileWatcher::Start(int poll_interval_ms) {
	if (running_.load(std::memory_order_acquire)) return;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"FileWatcher: starting, watching [{}] dir(s), interval=[{}ms]",
					watch_dirs_.size(),
					poll_interval_ms);

	running_.store(true, std::memory_order_release);
	thread_ = std::make_unique<std::thread>(
		[this, poll_interval_ms]() { WatchLoop(poll_interval_ms); });
}

void FileWatcher::Stop() {
	if (!running_.load(std::memory_order_acquire)) return;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "FileWatcher: stopping...");

	running_.store(false, std::memory_order_release);
	if (thread_ && thread_->joinable()) {
		thread_->join();
	}
	thread_.reset();

	ENGINE_LOG_INFO(logger, "FileWatcher: stopped");
}

void FileWatcher::WatchLoop(int poll_interval_ms) {
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "FileWatcher: watch loop started");

	while (running_.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));

		if (!running_.load(std::memory_order_acquire)) break;

		auto changed = ScanChanges();
		if (!changed.empty() && callback_) {
			callback_(changed);
		}
	}

	ENGINE_LOG_INFO(logger, "FileWatcher: watch loop exited");
}

std::vector<std::string> FileWatcher::ScanChanges() {
	std::vector<std::string> changed;
	std::error_code ec;

	for (const auto& dir : watch_dirs_) {
		if (!std::filesystem::exists(dir, ec)) continue;

		for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
			 it != std::filesystem::recursive_directory_iterator(); ++it) {
			if (ec) {
				ec.clear();
				continue;
			}

			const auto& entry = *it;
			if (!entry.is_regular_file(ec)) continue;
			if (ec) { ec.clear(); continue; }

			auto path_str = entry.path().string();
			if (!extension_.empty() && entry.path().extension().string() != extension_) {
				continue;
			}

			auto ftime = std::filesystem::last_write_time(entry, ec);
			if (ec) { ec.clear(); continue; }

			// Convert filesystem time_point to system_clock time_point
			auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				ftime - std::filesystem::file_time_type::clock::now() +
				std::chrono::system_clock::now());

			auto it_mtime = file_times_.find(path_str);
			if (it_mtime == file_times_.end()) {
				file_times_[path_str] = sctp;
			} else if (sctp > it_mtime->second) {
				it_mtime->second = sctp;
				changed.push_back(path_str);
			}
		}
	}

	return changed;
}

}  // namespace engine
