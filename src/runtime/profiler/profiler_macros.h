#pragma once

#ifdef ENGINE_PROFILER_ENABLED

#include "runtime/profiler/profiler_categories.h"
#include "runtime/profiler/profiler_switches.h"

#include <vector>

namespace engine::profiler_detail {

inline thread_local std::vector<unsigned char> g_profiler_manual_scope_stack;

inline void PushManualScope(bool enabled) {
	g_profiler_manual_scope_stack.push_back(enabled ? 1u : 0u);
}

inline bool PopManualScope() {
	if (g_profiler_manual_scope_stack.empty()) return false;
	auto enabled = g_profiler_manual_scope_stack.back() != 0u;
	g_profiler_manual_scope_stack.pop_back();
	return enabled;
}

}  // namespace engine::profiler_detail

#define ENGINE_PROFILE_INTERNAL_CONCAT2(a, b) a##b
#define ENGINE_PROFILE_INTERNAL_CONCAT(a, b) ENGINE_PROFILE_INTERNAL_CONCAT2(a, b)
#define ENGINE_PROFILE_INTERNAL_UID(prefix) ENGINE_PROFILE_INTERNAL_CONCAT(prefix, __LINE__)

#define ENGINE_PROFILE_INTERNAL_SCOPED_FINALIZER(category)              \
	struct ENGINE_PROFILE_INTERNAL_UID(EngineProfilerScopedEvent) {      \
		bool enabled;                                                    \
		~ENGINE_PROFILE_INTERNAL_UID(EngineProfilerScopedEvent)() {      \
			if (enabled) {                                               \
				TRACE_EVENT_END(category);                               \
			}                                                            \
		}                                                                \
	}

#define ENGINE_PROFILE_SCOPE_GROUP(group, cat, name, ...)                         \
	ENGINE_PROFILE_INTERNAL_SCOPED_FINALIZER(cat)                                 \
	ENGINE_PROFILE_INTERNAL_UID(engine_profile_scoped_event_) {                   \
		[&]() {                                                                  \
			const bool engine_profile_enabled__ =                                 \
				::engine::ProfilerRuntimeIsGroupEnabled(group);                  \
			if (engine_profile_enabled__) {                                       \
				TRACE_EVENT_BEGIN(cat, name, ##__VA_ARGS__);                     \
			}                                                                     \
			return engine_profile_enabled__;                                      \
		}()                                                                       \
	}

#define ENGINE_PROFILE_BEGIN_GROUP(group, cat, name, ...)                         \
	do {                                                                          \
		const bool engine_profile_enabled__ =                                      \
			::engine::ProfilerRuntimeIsGroupEnabled(group);                       \
		::engine::profiler_detail::PushManualScope(engine_profile_enabled__);      \
		if (engine_profile_enabled__) {                                           \
			TRACE_EVENT_BEGIN(cat, name, ##__VA_ARGS__);                          \
		}                                                                         \
	} while (0)

#define ENGINE_PROFILE_END_GROUP(group, cat)                                      \
	do {                                                                          \
		(void) (group);                                                           \
		if (::engine::profiler_detail::PopManualScope()) {                        \
			TRACE_EVENT_END(cat);                                                 \
		}                                                                         \
	} while (0)

#define ENGINE_PROFILE_INSTANT_GROUP(group, cat, name, ...)                       \
	do {                                                                          \
		if (::engine::ProfilerRuntimeIsGroupEnabled(group)) {                     \
			TRACE_EVENT_INSTANT(cat, name, ##__VA_ARGS__);                        \
		}                                                                         \
	} while (0)

#define ENGINE_PROFILE_COUNTER_GROUP(group, cat, name, value)                     \
	do {                                                                          \
		if (::engine::ProfilerRuntimeIsGroupEnabled(group)) {                     \
			TRACE_COUNTER(cat, perfetto::CounterTrack(name), value);              \
		}                                                                         \
	} while (0)

#define ENGINE_PROFILE_COUNTER_TRACK_GROUP(group, cat, track, value)              \
	do {                                                                          \
		if (::engine::ProfilerRuntimeIsGroupEnabled(group)) {                     \
			TRACE_COUNTER(cat, track, value);                                     \
		}                                                                         \
	} while (0)

// RAII scope event. Prefer this for most code paths.
// cat and name should normally be string literals.
#define ENGINE_PROFILE_SCOPE(cat, name, ...) \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat, name, ##__VA_ARGS__)

// Manual begin/end pair for cross-scope events such as frame-level ranges.
#define ENGINE_PROFILE_BEGIN(cat, name, ...) \
	ENGINE_PROFILE_BEGIN_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat, name, ##__VA_ARGS__)
#define ENGINE_PROFILE_END(cat) \
	ENGINE_PROFILE_END_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat)

// Instant event with optional debug annotations.
#define ENGINE_PROFILE_INSTANT(cat, name, ...) \
	ENGINE_PROFILE_INSTANT_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat, name, ##__VA_ARGS__)

// Counter with a new track each call. For continuous series use COUNTER_TRACK.
#define ENGINE_PROFILE_COUNTER(cat, name, value) \
	ENGINE_PROFILE_COUNTER_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat, name, value)

// Counter with a pre-created track.
#define ENGINE_PROFILE_COUNTER_TRACK(cat, track, value) \
	ENGINE_PROFILE_COUNTER_TRACK_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat, track, value)

// Function-level auto-scope helpers.
#define ENGINE_PROFILE_FUNC() ENGINE_PROFILE_SCOPE("engine", __func__)
#define ENGINE_PROFILE_FUNC_CAT(cat) ENGINE_PROFILE_SCOPE(cat, __func__)

// Flow events for cross-thread causality.
#define ENGINE_PROFILE_FLOW_BEGIN(cat, name, flow_id) \
	ENGINE_PROFILE_BEGIN_GROUP(::engine::ProfilerEventGroupFromCategory(cat), \
							   cat,                                          \
							   name,                                         \
							   perfetto::Flow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#define ENGINE_PROFILE_FLOW_END(cat) \
	ENGINE_PROFILE_END_GROUP(::engine::ProfilerEventGroupFromCategory(cat), cat)

#define ENGINE_PROFILE_FLOW_TERMINATE(cat, name, flow_id) \
	ENGINE_PROFILE_SCOPE_GROUP(                          \
		::engine::ProfilerEventGroupFromCategory(cat),    \
		cat,                                              \
		name,                                             \
		perfetto::TerminatingFlow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#else  // ENGINE_PROFILER_ENABLED not defined

// Empty macros intentionally do not evaluate arguments.
#define ENGINE_PROFILE_SCOPE_GROUP(group, cat, name, ...) \
	do {                                                  \
	} while (0)
#define ENGINE_PROFILE_BEGIN_GROUP(group, cat, name, ...) \
	do {                                                  \
	} while (0)
#define ENGINE_PROFILE_END_GROUP(group, cat) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_INSTANT_GROUP(group, cat, name, ...) \
	do {                                                    \
	} while (0)
#define ENGINE_PROFILE_COUNTER_GROUP(group, cat, name, value) \
	do {                                                      \
	} while (0)
#define ENGINE_PROFILE_COUNTER_TRACK_GROUP(group, cat, track, value) \
	do {                                                            \
	} while (0)
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
