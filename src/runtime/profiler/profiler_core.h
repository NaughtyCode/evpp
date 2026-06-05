#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/profiler/profiler_switches.h"

namespace perfetto {
class TracingSession;
}

namespace engine {

struct CLOUD_ENGINE_API ProfilerConfig {
	std::string output_path = "trace.perfetto-trace";
	uint32_t buffer_size_kb = 32768;
	uint32_t duration_ms = 0;
	uint32_t flush_interval_ms = 5000;
	bool write_into_file = false;
	bool runtime_enabled = true;
	ProfilerEventGroupMask enabled_event_groups = kProfilerAllEventGroups;
};

class CLOUD_ENGINE_API ProfilerManager {
	public:
	static ProfilerManager& Get();

	bool Initialize(const ProfilerConfig& cfg);
	void Shutdown();

	bool StartSession();
	void StopSession();

	bool IsInitialized() const;
	bool IsActive() const;
	static bool IsEnabled();

	void SetRuntimeEnabled(bool enabled);
	bool IsRuntimeEnabled() const;
	void SetEnabledEventGroups(ProfilerEventGroupMask mask);
	void EnableEventGroups(ProfilerEventGroupMask mask);
	void DisableEventGroups(ProfilerEventGroupMask mask);
	void SetEventGroupEnabled(ProfilerEventGroup group, bool enabled);
	bool IsEventGroupEnabled(ProfilerEventGroup group) const;
	ProfilerEventGroupMask EnabledEventGroups() const;

	void Flush();
	std::vector<char> ReadTrace();
	std::string SaveTrace();
	bool SaveTraceExact(const std::string& path);
	std::string LastSavedPath() const;
	size_t CachedTraceSize() const;
	void ClearCachedTrace();

	private:
	ProfilerManager();
	~ProfilerManager();

	ProfilerManager(const ProfilerManager&) = delete;
	ProfilerManager& operator=(const ProfilerManager&) = delete;

	static ProfilerConfig NormalizeConfig(ProfilerConfig cfg);
	std::string BuildTimestampedPathLocked() const;
	void StopSessionLocked();
	std::vector<char> ReadTraceLocked();
	bool WriteTraceToFileLocked(const std::string& path, const std::vector<char>& data);

	mutable std::mutex mutex_;
	ProfilerConfig config_;
	std::unique_ptr<perfetto::TracingSession> session_;
	std::vector<char> cached_trace_;
	std::string last_saved_path_;
	bool tracing_runtime_initialized_ = false;
	std::atomic<bool> initialized_{false};
	std::atomic<bool> session_active_{false};
};

}  // namespace engine
