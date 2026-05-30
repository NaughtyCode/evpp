#pragma once

#ifdef ENGINE_PROFILER_ENABLED

#include "runtime/profiler/profiler_categories.h"

// RAII scope event. Prefer this for most code paths.
// cat and name should normally be string literals.
#define ENGINE_PROFILE_SCOPE(cat, name, ...) TRACE_EVENT(cat, name, ##__VA_ARGS__)

// Manual begin/end pair for cross-scope events such as frame-level ranges.
#define ENGINE_PROFILE_BEGIN(cat, name, ...) TRACE_EVENT_BEGIN(cat, name, ##__VA_ARGS__)
#define ENGINE_PROFILE_END(cat) TRACE_EVENT_END(cat)

// Instant event with optional debug annotations.
#define ENGINE_PROFILE_INSTANT(cat, name, ...) TRACE_EVENT_INSTANT(cat, name, ##__VA_ARGS__)

// Counter with a new track each call. For continuous series use COUNTER_TRACK.
#define ENGINE_PROFILE_COUNTER(cat, name, value) \
	TRACE_COUNTER(cat, perfetto::CounterTrack(name), value)

// Counter with a pre-created track.
#define ENGINE_PROFILE_COUNTER_TRACK(cat, track, value) TRACE_COUNTER(cat, track, value)

// Function-level auto-scope helpers.
#define ENGINE_PROFILE_FUNC() ENGINE_PROFILE_SCOPE("engine", __func__)
#define ENGINE_PROFILE_FUNC_CAT(cat) ENGINE_PROFILE_SCOPE(cat, __func__)

// Flow events for cross-thread causality.
#define ENGINE_PROFILE_FLOW_BEGIN(cat, name, flow_id) \
	TRACE_EVENT_BEGIN(cat, name, perfetto::Flow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#define ENGINE_PROFILE_FLOW_END(cat) TRACE_EVENT_END(cat)

#define ENGINE_PROFILE_FLOW_TERMINATE(cat, name, flow_id) \
	TRACE_EVENT(cat, name, perfetto::TerminatingFlow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#else  // ENGINE_PROFILER_ENABLED not defined

// Empty macros intentionally do not evaluate arguments.
#define ENGINE_PROFILE_SCOPE(cat, name, ...) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_BEGIN(cat, name, ...) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_END(cat) \
	do {                        \
	} while (0)
#define ENGINE_PROFILE_INSTANT(cat, name, ...) \
	do {                                       \
	} while (0)
#define ENGINE_PROFILE_COUNTER(cat, name, value) \
	do {                                         \
	} while (0)
#define ENGINE_PROFILE_COUNTER_TRACK(cat, track, value) \
	do {                                                \
	} while (0)
#define ENGINE_PROFILE_FUNC() \
	do {                      \
	} while (0)
#define ENGINE_PROFILE_FUNC_CAT(cat) \
	do {                             \
	} while (0)
#define ENGINE_PROFILE_FLOW_BEGIN(cat, name, flow_id) \
	do {                                              \
	} while (0)
#define ENGINE_PROFILE_FLOW_END(cat) \
	do {                             \
	} while (0)
#define ENGINE_PROFILE_FLOW_TERMINATE(cat, name, flow_id) \
	do {                                                  \
	} while (0)

#endif  // ENGINE_PROFILER_ENABLED
