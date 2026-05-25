#ifdef ENGINE_PROFILER_ENABLED

#include "thirdparty/perfetto/perfetto.h"

// Category declarations must be visible before STATIC_STORAGE since
// the storage expansion references kCategoryCount, kCategories, etc.
#include "runtime/profiler/profiler_categories.h"

// Must appear in global scope, before any namespace, in exactly one .cc file.
// Must be in the same scope as PERFETTO_DEFINE_CATEGORIES (global scope).
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

#include "runtime/profiler/profiler_core.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

namespace engine {

// ── Singleton ───────────────────────────────────────────────────────────

ProfilerManager& ProfilerManager::Get() {
    static ProfilerManager instance;
    return instance;
}

ProfilerManager::ProfilerManager() = default;
ProfilerManager::~ProfilerManager() = default;

// ── Initialize / Shutdown ───────────────────────────────────────────────

bool ProfilerManager::Initialize(const ProfilerConfig& cfg) {
    if (initialized_) return true;

    config_ = cfg;

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ProfilerManager: initializing (before Tracing::Init)");

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = cfg.buffer_size_kb;

    perfetto::Tracing::Initialize(args);

    ENGINE_LOG_INFO(logger, "ProfilerManager: after Tracing::Init, before Register");

    perfetto::TrackEvent::Register();

    ENGINE_LOG_INFO(logger, "ProfilerManager: initialized, buffer=[{}KB]", cfg.buffer_size_kb);

    initialized_ = true;
    return true;
}

void ProfilerManager::Shutdown() {
    if (!initialized_) return;

    Flush();
    StopSession();
    initialized_ = false;

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ProfilerManager: shutdown complete");
}

// ── Session Control ────────────────────────────────────────────────────

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
    tc.set_flush_period_ms(cfg.flush_interval_ms);
    return tc;
}

bool ProfilerManager::StartSession() {
    if (!initialized_) return false;

    if (session_) {
        StopSession();
    }

    auto cfg = MakeTraceConfig(config_);
    session_ = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    if (!session_) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger, "ProfilerManager: NewTrace returned nullptr");
        return false;
    }
    session_->Setup(cfg);
    session_->StartBlocking();
    session_active_ = true;

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ProfilerManager: session started");
    return true;
}

void ProfilerManager::StopSession() {
    if (!session_) return;

    session_->StopBlocking();
    session_active_ = false;

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ProfilerManager: session stopped");
}

// ── State Queries ──────────────────────────────────────────────────────

bool ProfilerManager::IsActive() const {
    return session_active_;
}

bool ProfilerManager::IsEnabled() {
#ifdef ENGINE_PROFILER_ENABLED
    return true;
#else
    return false;
#endif
}

// ── Flush ───────────────────────────────────────────────────────────────

void ProfilerManager::Flush() {
    if (session_) {
        session_->FlushBlocking();
    }
}

// ── ReadTrace ───────────────────────────────────────────────────────────

std::vector<char> ProfilerManager::ReadTrace() {
    if (!session_) return {};
    return session_->ReadTraceBlocking();
}

// ── SaveTrace (with timestamp suffix) ───────────────────────────────────

void ProfilerManager::SaveTrace() {
    auto data = ReadTrace();
    if (data.empty()) {
        auto* logger = GetLogger();
        ENGINE_LOG_WARN(logger, "ProfilerManager: SaveTrace called but no data available");
        return;
    }

    // Generate timestamp suffix: "trace_20260524_143021.perfetto-trace"
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");

    // Insert timestamp before the extension
    std::string path = config_.output_path;
    auto dot = path.rfind('.');
    std::string stamped;
    if (dot != std::string::npos) {
        stamped = path.substr(0, dot) + "_" + oss.str() + path.substr(dot);
    } else {
        stamped = path + "_" + oss.str();
    }

    WriteTraceToFile(stamped, data);
}

void ProfilerManager::SaveTraceExact(const std::string& path) {
    auto data = ReadTrace();
    if (data.empty()) {
        auto* logger = GetLogger();
        ENGINE_LOG_WARN(logger,
            "ProfilerManager: SaveTraceExact [{}] called but no data available",
            path);
        return;
    }

    WriteTraceToFile(path, data);
}

void ProfilerManager::WriteTraceToFile(const std::string& path,
                                        const std::vector<char>& data) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        auto* logger = GetLogger();
        ENGINE_LOG_ERROR(logger,
            "ProfilerManager: failed to open trace file [{}]", path);
        return;
    }

    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    ofs.close();

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger,
        "ProfilerManager: trace saved to [{}] ([{}] bytes)",
        path, data.size());
}

} // namespace engine

#else // ENGINE_PROFILER_ENABLED not defined

// Stub implementations — all methods are no-ops.
// perfetto.h is included here (even though no tracing macros are used)
// so that std::unique_ptr<perfetto::TracingSession> in the header
// can be properly compiled and destroyed.

#include "runtime/profiler/profiler_core.h"

#include "thirdparty/perfetto/perfetto.h"

#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"

namespace engine {

ProfilerManager& ProfilerManager::Get() {
    static ProfilerManager instance;
    return instance;
}

ProfilerManager::ProfilerManager() = default;
ProfilerManager::~ProfilerManager() = default;

bool ProfilerManager::Initialize(const ProfilerConfig& cfg) {
    (void)cfg;
    return true;
}

void ProfilerManager::Shutdown() {}

bool ProfilerManager::StartSession() { return false; }

void ProfilerManager::StopSession() {}

bool ProfilerManager::IsActive() const { return false; }

bool ProfilerManager::IsEnabled() { return false; }

void ProfilerManager::Flush() {}

std::vector<char> ProfilerManager::ReadTrace() { return {}; }

void ProfilerManager::SaveTrace() {}

void ProfilerManager::SaveTraceExact(const std::string& path) {
    (void)path;
}

} // namespace engine

#endif // ENGINE_PROFILER_ENABLED
