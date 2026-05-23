#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine_export.h"

namespace perfetto {
class TracingSession;
}

namespace engine {

struct ENGINE_API ProfilerConfig {
    std::string output_path       = "trace.perfetto-trace";
    uint32_t    buffer_size_kb    = 32768;
    uint32_t    duration_ms       = 0;
    uint32_t    flush_interval_ms = 5000;
    bool        write_into_file   = false;
};

class ENGINE_API ProfilerManager {
public:
    static ProfilerManager& Get();

    bool   Initialize(const ProfilerConfig& cfg);
    void   Shutdown();

    bool   StartSession();
    void   StopSession();

    bool   IsActive() const;
    static bool IsEnabled();

    void              Flush();
    std::vector<char> ReadTrace();
    void              SaveTrace();
    void              SaveTraceExact(const std::string& path);

private:
    ProfilerManager();
    ~ProfilerManager();

    ProfilerManager(const ProfilerManager&) = delete;
    ProfilerManager& operator=(const ProfilerManager&) = delete;

    void WriteTraceToFile(const std::string& path, const std::vector<char>& data);

    ProfilerConfig config_;
    std::unique_ptr<perfetto::TracingSession> session_;
    bool initialized_ = false;
};

} // namespace engine
