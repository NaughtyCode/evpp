#ifdef ENGINE_PROFILER_ENABLED

#include "thirdparty/perfetto/perfetto.h"

// Category declarations must be visible before STATIC_STORAGE since
// the storage expansion references kCategoryCount, kCategories, etc.
#include "runtime/profiler/profiler_categories.h"

// Must appear in global scope, before any namespace, in exactly one .cc file.
// Must be in the same scope as PERFETTO_DEFINE_CATEGORIES (global scope).
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/profiler/profiler_core.h"

namespace engine {

namespace {

constexpr uint32_t kDefaultBufferSizeKb = 32768;
constexpr uint32_t kMinBufferSizeKb = 64;
constexpr const char* kDefaultOutputPath = "trace.perfetto-trace";

std::string TimestampForFilename() {
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);
	std::tm tm_buf{};
#ifdef _WIN32
	if (localtime_s(&tm_buf, &t) != 0) {
		return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
								  now.time_since_epoch())
								  .count());
	}
#else
	if (!localtime_r(&t, &tm_buf)) {
		return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
								  now.time_since_epoch())
								  .count());
	}
#endif

	std::ostringstream oss;
	oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
	return oss.str();
}

}  // namespace

ProfilerManager& ProfilerManager::Get() {
	static ProfilerManager instance;
	return instance;
}

ProfilerManager::ProfilerManager() = default;

ProfilerManager::~ProfilerManager() {
	Shutdown();
}

bool ProfilerManager::Initialize(const ProfilerConfig& cfg) {
	auto normalized = NormalizeConfig(cfg);
	auto* logger = GetLogger();

	std::lock_guard<std::mutex> lock(mutex_);
	if (session_) {
		ENGINE_LOG_WARN(logger, "ProfilerManager: Initialize ignored while session is active");
		return false;
	}

	config_ = std::move(normalized);
	ProfilerRuntimeSetEnabled(config_.runtime_enabled);
	ProfilerRuntimeSetEnabledGroups(config_.enabled_event_groups);

	if (!tracing_runtime_initialized_) {
		ENGINE_LOG_INFO(logger, "ProfilerManager: initializing Perfetto runtime");

		perfetto::TracingInitArgs args;
		args.backends = perfetto::kInProcessBackend;
		args.shmem_size_hint_kb = config_.buffer_size_kb;

		perfetto::Tracing::Initialize(args);
		perfetto::TrackEvent::Register();

		tracing_runtime_initialized_ = true;
	} else {
		ENGINE_LOG_INFO(logger, "ProfilerManager: updating profiler config");
	}

	initialized_.store(true);

	ENGINE_LOG_INFO(logger,
					"ProfilerManager: initialized, buffer=[{}KB], output=[{}]",
					config_.buffer_size_kb,
					config_.output_path);

	return true;
}

void ProfilerManager::Shutdown() {
	auto* logger = GetLogger();

	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_.load() && !session_) return;

	StopSessionLocked();
	initialized_.store(false);
	ENGINE_LOG_INFO(logger, "ProfilerManager: shutdown complete");
}

static perfetto::TraceConfig MakeTraceConfig(const ProfilerConfig& cfg) {
	perfetto::TraceConfig tc;
	auto* buf = tc.add_buffers();
	buf->set_size_kb(cfg.buffer_size_kb);
	buf->set_fill_policy(perfetto::TraceConfig::BufferConfig::RING_BUFFER);

	auto* ds = tc.add_data_sources();
	ds->mutable_config()->set_name("track_event");

	if (cfg.duration_ms > 0) {
		tc.set_duration_ms(cfg.duration_ms);
	}
	if (cfg.flush_interval_ms > 0) {
		tc.set_flush_period_ms(cfg.flush_interval_ms);
	}
	return tc;
}

bool ProfilerManager::StartSession() {
	auto* logger = GetLogger();
	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_.load()) {
		ENGINE_LOG_WARN(logger, "ProfilerManager: StartSession called before Initialize");
		return false;
	}

	if (session_) {
		StopSessionLocked();
	}

	auto cfg = MakeTraceConfig(config_);
	session_ = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
	if (!session_) {
		ENGINE_LOG_ERROR(logger, "ProfilerManager: NewTrace returned nullptr");
		return false;
	}

	session_->Setup(cfg);
	session_->StartBlocking();
	session_active_.store(true);
	cached_trace_.clear();
	last_saved_path_.clear();

	ENGINE_LOG_INFO(logger, "ProfilerManager: session started");
	return true;
}

void ProfilerManager::StopSession() {
	std::lock_guard<std::mutex> lock(mutex_);
	StopSessionLocked();
}

void ProfilerManager::StopSessionLocked() {
	if (!session_) {
		session_active_.store(false);
		return;
	}

	session_->StopBlocking();
	cached_trace_ = session_->ReadTraceBlocking();
	session_.reset();
	session_active_.store(false);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ProfilerManager: session stopped, cached trace=[{}] bytes",
					cached_trace_.size());

	if (config_.write_into_file && !cached_trace_.empty()) {
		if (!WriteTraceToFileLocked(config_.output_path, cached_trace_)) {
			ENGINE_LOG_ERROR(logger,
							 "ProfilerManager: failed to auto-save trace to [{}]",
							 config_.output_path);
		}
	}
}

bool ProfilerManager::IsInitialized() const {
	return initialized_.load();
}

bool ProfilerManager::IsActive() const {
	return session_active_.load();
}

bool ProfilerManager::IsEnabled() {
#ifdef ENGINE_PROFILER_ENABLED
	return true;
#else
	return false;
#endif
}

void ProfilerManager::SetRuntimeEnabled(bool enabled) {
	ProfilerRuntimeSetEnabled(enabled);
}

bool ProfilerManager::IsRuntimeEnabled() const {
	return ProfilerRuntimeIsEnabled();
}

void ProfilerManager::SetEnabledEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeSetEnabledGroups(mask);
}

void ProfilerManager::EnableEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeEnableGroups(mask);
}

void ProfilerManager::DisableEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeDisableGroups(mask);
}

void ProfilerManager::SetEventGroupEnabled(ProfilerEventGroup group, bool enabled) {
	ProfilerRuntimeSetGroupEnabled(group, enabled);
}

bool ProfilerManager::IsEventGroupEnabled(ProfilerEventGroup group) const {
	return ProfilerRuntimeIsGroupEnabled(group);
}

ProfilerEventGroupMask ProfilerManager::EnabledEventGroups() const {
	return ProfilerRuntimeEnabledGroups();
}

void ProfilerManager::Flush() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (session_) {
		session_->FlushBlocking();
	}
}

std::vector<char> ProfilerManager::ReadTrace() {
	std::lock_guard<std::mutex> lock(mutex_);
	return ReadTraceLocked();
}

std::vector<char> ProfilerManager::ReadTraceLocked() {
	if (session_) {
		// Avoid ReadTraceBlocking() while the session is still active: it can
		// block until the trace is stopped. StopSessionLocked() is the capture
		// point that materializes cached_trace_ for saving.
		session_->FlushBlocking();
	}
	return cached_trace_;
}

std::string ProfilerManager::SaveTrace() {
	std::lock_guard<std::mutex> lock(mutex_);
	auto data = ReadTraceLocked();
	if (data.empty()) {
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger, "ProfilerManager: SaveTrace called but no data available");
		return {};
	}

	auto stamped = BuildTimestampedPathLocked();
	if (!WriteTraceToFileLocked(stamped, data)) return {};
	return stamped;
}

bool ProfilerManager::SaveTraceExact(const std::string& path) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto data = ReadTraceLocked();
	if (data.empty()) {
		auto* logger = GetLogger();
		ENGINE_LOG_WARN(
			logger, "ProfilerManager: SaveTraceExact [{}] called but no data available", path);
		return false;
	}

	return WriteTraceToFileLocked(path, data);
}

std::string ProfilerManager::LastSavedPath() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return last_saved_path_;
}

size_t ProfilerManager::CachedTraceSize() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return cached_trace_.size();
}

void ProfilerManager::ClearCachedTrace() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (session_) return;
	cached_trace_.clear();
	last_saved_path_.clear();
}

ProfilerConfig ProfilerManager::NormalizeConfig(ProfilerConfig cfg) {
	if (cfg.output_path.empty()) {
		cfg.output_path = kDefaultOutputPath;
	}
	if (cfg.buffer_size_kb == 0) {
		cfg.buffer_size_kb = kDefaultBufferSizeKb;
	} else if (cfg.buffer_size_kb < kMinBufferSizeKb) {
		cfg.buffer_size_kb = kMinBufferSizeKb;
	}
	cfg.enabled_event_groups &= kProfilerAllEventGroups;
	return cfg;
}

std::string ProfilerManager::BuildTimestampedPathLocked() const {
	std::filesystem::path base(config_.output_path.empty() ? kDefaultOutputPath : config_.output_path);
	auto parent = base.parent_path();
	auto stem = base.stem().string();
	auto extension = base.extension().string();
	if (stem.empty()) {
		stem = base.filename().string();
	}
	if (stem.empty()) {
		stem = "trace";
	}

	auto timestamp = TimestampForFilename();
	auto make_candidate = [&](int suffix) {
		std::string filename = stem + "_" + timestamp;
		if (suffix > 0) filename += "_" + std::to_string(suffix);
		filename += extension;
		return parent.empty() ? std::filesystem::path(filename) : parent / filename;
	};

	for (int i = 0; i < 1000; ++i) {
		auto candidate = make_candidate(i);
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec)) {
			return candidate.string();
		}
	}

	return make_candidate(1000).string();
}

bool ProfilerManager::WriteTraceToFileLocked(const std::string& path,
											 const std::vector<char>& data) {
	if (path.empty() || data.empty()) return false;

	std::filesystem::path file_path(path);
	auto parent = file_path.parent_path();
	if (!parent.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger,
							 "ProfilerManager: failed to create trace directory [{}]: {}",
							 parent.string(),
							 ec.message());
			return false;
		}
	}

	std::ofstream ofs(file_path, std::ios::binary | std::ios::trunc);
	if (!ofs) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ProfilerManager: failed to open trace file [{}]", path);
		return false;
	}

	ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
	if (!ofs) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ProfilerManager: failed to write trace file [{}]", path);
		return false;
	}
	ofs.close();
	if (!ofs) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "ProfilerManager: failed to close trace file [{}]", path);
		return false;
	}

	last_saved_path_ = file_path.string();
	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger,
					"ProfilerManager: trace saved to [{}] ([{}] bytes)",
					last_saved_path_,
					data.size());
	return true;
}

}  // namespace engine

#else  // ENGINE_PROFILER_ENABLED not defined

// Stub implementations: all methods are no-ops.
// perfetto.h is included here so the unique_ptr<perfetto::TracingSession>
// member in the header can be properly compiled and destroyed.

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/profiler/profiler_core.h"

#include "thirdparty/perfetto/perfetto.h"

namespace engine {

ProfilerManager& ProfilerManager::Get() {
	static ProfilerManager instance;
	return instance;
}

ProfilerManager::ProfilerManager() = default;
ProfilerManager::~ProfilerManager() = default;

bool ProfilerManager::Initialize(const ProfilerConfig& cfg) {
	auto normalized = NormalizeConfig(cfg);
	ProfilerRuntimeSetEnabled(normalized.runtime_enabled);
	ProfilerRuntimeSetEnabledGroups(normalized.enabled_event_groups);
	return true;
}

void ProfilerManager::Shutdown() {
}

bool ProfilerManager::StartSession() {
	return false;
}

void ProfilerManager::StopSession() {
}

bool ProfilerManager::IsInitialized() const {
	return false;
}

bool ProfilerManager::IsActive() const {
	return false;
}

bool ProfilerManager::IsEnabled() {
	return false;
}

void ProfilerManager::SetRuntimeEnabled(bool enabled) {
	ProfilerRuntimeSetEnabled(enabled);
}

bool ProfilerManager::IsRuntimeEnabled() const {
	return ProfilerRuntimeIsEnabled();
}

void ProfilerManager::SetEnabledEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeSetEnabledGroups(mask);
}

void ProfilerManager::EnableEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeEnableGroups(mask);
}

void ProfilerManager::DisableEventGroups(ProfilerEventGroupMask mask) {
	ProfilerRuntimeDisableGroups(mask);
}

void ProfilerManager::SetEventGroupEnabled(ProfilerEventGroup group, bool enabled) {
	ProfilerRuntimeSetGroupEnabled(group, enabled);
}

bool ProfilerManager::IsEventGroupEnabled(ProfilerEventGroup group) const {
	return ProfilerRuntimeIsGroupEnabled(group);
}

ProfilerEventGroupMask ProfilerManager::EnabledEventGroups() const {
	return ProfilerRuntimeEnabledGroups();
}

void ProfilerManager::Flush() {
}

std::vector<char> ProfilerManager::ReadTrace() {
	return {};
}

std::string ProfilerManager::SaveTrace() {
	return {};
}

bool ProfilerManager::SaveTraceExact(const std::string& path) {
	(void) path;
	return false;
}

std::string ProfilerManager::LastSavedPath() const {
	return {};
}

size_t ProfilerManager::CachedTraceSize() const {
	return 0;
}

void ProfilerManager::ClearCachedTrace() {
}

ProfilerConfig ProfilerManager::NormalizeConfig(ProfilerConfig cfg) {
	cfg.enabled_event_groups &= kProfilerAllEventGroups;
	return cfg;
}

std::string ProfilerManager::BuildTimestampedPathLocked() const {
	return {};
}

void ProfilerManager::StopSessionLocked() {
}

std::vector<char> ProfilerManager::ReadTraceLocked() {
	return {};
}

bool ProfilerManager::WriteTraceToFileLocked(const std::string& path,
											 const std::vector<char>& data) {
	(void) path;
	(void) data;
	return false;
}

}  // namespace engine

#endif  // ENGINE_PROFILER_ENABLED
