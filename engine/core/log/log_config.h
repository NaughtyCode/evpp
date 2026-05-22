#pragma once

#include <quill/backend/BackendOptions.h>
#include <quill/core/FrontendOptions.h>

namespace engine {

inline quill::BackendOptions GetBackendOptions() {
    quill::BackendOptions opts;
    opts.sleep_duration = std::chrono::microseconds{500};
    opts.transit_events_soft_limit = 16384;
    opts.sink_min_flush_interval = std::chrono::milliseconds{100};
    return opts;
}

} // namespace engine
