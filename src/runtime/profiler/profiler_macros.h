#pragma once

#ifdef ENGINE_PROFILER_ENABLED

#include "runtime/profiler/profiler_categories.h"

// ── RAII scope event (preferred for most uses) ──────────────────────────
// cat: string literal category name, e.g. "engine"
// name: event name string literal
// ...: optional debug annotations (key-value pairs) or flow/track annotations
#define ENGINE_PROFILE_SCOPE(cat, name, ...) TRACE_EVENT(cat, name, ##__VA_ARGS__)

// ── Manual begin/end pair (cross-scope, e.g. frame-level pairing) ──────
#define ENGINE_PROFILE_BEGIN(cat, name, ...) TRACE_EVENT_BEGIN(cat, name, ##__VA_ARGS__)

#define ENGINE_PROFILE_END(cat) TRACE_EVENT_END(cat)

// ── Instant event (zero-duration marker) ────────────────────────────────
#define ENGINE_PROFILE_INSTANT(cat, name) TRACE_EVENT_INSTANT(cat, name)

// ── Counter (new track UUID each call — use for one-shot values) ────────
#define ENGINE_PROFILE_COUNTER(cat, name, value) \
	TRACE_COUNTER(cat, perfetto::CounterTrack(name), value)

// ── Counter with pre-created track (continuous time series) ─────────────
#define ENGINE_PROFILE_COUNTER_TRACK(cat, track, value) TRACE_COUNTER(cat, track, value)

// ── Function-level auto-scope ───────────────────────────────────────────
#define ENGINE_PROFILE_FUNC() ENGINE_PROFILE_SCOPE("engine", __func__)

// ── Flow events (cross-thread causality) ────────────────────────────────
// flow_id is typically frame_count_ (uint64_t), shared between threads
#define ENGINE_PROFILE_FLOW_BEGIN(cat, name, flow_id) \
	TRACE_EVENT_BEGIN(cat, name, perfetto::Flow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#define ENGINE_PROFILE_FLOW_END(cat) TRACE_EVENT_END(cat)

#define ENGINE_PROFILE_FLOW_TERMINATE(cat, name, flow_id) \
	TRACE_EVENT_BEGIN(                                    \
		cat, name, perfetto::TerminatingFlow::ProcessScoped(static_cast<uint64_t>(flow_id)))

#else  // ENGINE_PROFILER_ENABLED not defined

// All macros expand to empty — zero runtime and compile-time cost
#define ENGINE_PROFILE_SCOPE(cat, name, ...) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_BEGIN(cat, name, ...) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_END(cat) \
	do {                        \
	} while (0)
#define ENGINE_PROFILE_INSTANT(cat, name) \
	do {                                  \
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
#define ENGINE_PROFILE_FLOW_BEGIN(cat, name, flow_id) \
	do {                                              \
	} while (0)
#define ENGINE_PROFILE_FLOW_END(cat) \
	do {                             \
	} while (0)
#define ENGINE_PROFILE_FLOW_TERMINATE(cat, name, flow_id) \
	do {                                                  \
	} while (0)

#endif	// ENGINE_PROFILER_ENABLED
