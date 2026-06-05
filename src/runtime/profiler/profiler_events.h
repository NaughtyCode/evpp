#pragma once

#include "runtime/profiler/profiler_macros.h"

#ifdef ENGINE_PROFILER_ENABLED

#include "thirdparty/perfetto/perfetto.h"

// ── Reusable counter tracks for frame-level metrics ─────────────────────
// Static tracks ensure continuous time series in the trace UI rather than
// one track per call. Declared inline to satisfy ODR.

inline perfetto::CounterTrack g_frame_no_track("frame_number");
inline perfetto::CounterTrack g_frame_dt_track("frame_delta_ms");
inline perfetto::CounterTrack g_frame_time_track("frame_elapsed_ms");

// ── Thread-local storage for physics flow event ─────────────────────────
inline thread_local uint64_t g_profiler_physics_frame_id = UINT64_MAX;

// ── Engine lifecycle ────────────────────────────────────────────────────

#define ENGINE_PROFILE_TICK() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Frame, "engine", "Tick")

#define ENGINE_PROFILE_FRAME_BEGIN(frame_no, dt_ms)                                               \
	do {                                                                                          \
		ENGINE_PROFILE_COUNTER_TRACK_GROUP(                                                        \
			::engine::ProfilerEventGroup::Frame, "engine", g_frame_no_track,                       \
			static_cast<int64_t>(frame_no));                                                       \
		ENGINE_PROFILE_COUNTER_TRACK_GROUP(                                                        \
			::engine::ProfilerEventGroup::Frame, "engine", g_frame_dt_track,                       \
			static_cast<int64_t>(dt_ms));                                                          \
		ENGINE_PROFILE_BEGIN_GROUP(::engine::ProfilerEventGroup::Frame,                            \
								   "engine",                                                       \
								   "Frame",                                                        \
								   "frame_no",                                                     \
								   static_cast<int64_t>(frame_no),                                 \
								   "dt_ms",                                                        \
								   static_cast<int64_t>(dt_ms));                                   \
	} while (0)

#define ENGINE_PROFILE_FRAME_END(elapsed_ms)                                 \
	do {                                                                     \
		ENGINE_PROFILE_COUNTER_TRACK_GROUP(::engine::ProfilerEventGroup::Frame,                    \
										   "engine",                                               \
										   g_frame_time_track,                                      \
										   static_cast<int64_t>(elapsed_ms));                       \
		ENGINE_PROFILE_END_GROUP(::engine::ProfilerEventGroup::Frame, "engine");                  \
	} while (0)

#define ENGINE_PROFILE_SLOW_FRAME(elapsed_ms, threshold_ms)                       \
	do {                                                                         \
		if (::engine::ProfilerRuntimeIsGroupEnabled(::engine::ProfilerEventGroup::Frame)) { \
			const auto engine_profile_elapsed_ms__ = (elapsed_ms);               \
			const auto engine_profile_threshold_ms__ = (threshold_ms);           \
			if (engine_profile_elapsed_ms__ > engine_profile_threshold_ms__) {   \
				ENGINE_PROFILE_INSTANT_GROUP(                                    \
					::engine::ProfilerEventGroup::Frame,                         \
					"engine",                                                   \
					"SlowFrame",                                                \
					"elapsed_ms",                                               \
					static_cast<int64_t>(engine_profile_elapsed_ms__),           \
					"threshold_ms",                                             \
					static_cast<int64_t>(engine_profile_threshold_ms__));        \
			}                                                                    \
		}                                                                        \
	} while (0)

// ── Frame sub-stages ────────────────────────────────────────────────────

#define ENGINE_PROFILE_TIMER_UPDATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Timer, "engine.timer", "TimerUpdate")

#define ENGINE_PROFILE_PHYSICS_TICK(frame_no)            \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, \
							   "engine.physics",                     \
							   "PhysicsTick",                        \
							   "frame_no",                           \
							   static_cast<int64_t>(frame_no),        \
							   perfetto::Flow::ProcessScoped(static_cast<uint64_t>(frame_no)))

#define ENGINE_PROFILE_SCRIPT_UPDATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Script, "engine.script", "ScriptUpdate")

#define ENGINE_PROFILE_PHYSICS_FETCH(frame_no) \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, \
							   "engine.physics",                     \
							   "PhysicsFetch",                       \
							   "frame_no",                           \
							   static_cast<int64_t>(frame_no))

#define ENGINE_PROFILE_SCRIPT_CALLBACK() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Script, "engine.script", "ScriptCallback")

// ── Physics thread ──────────────────────────────────────────────────────

#define ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE(command)                                            \
	do {                                                                                       \
		if ((command).type == CommandType::Tick) {                                             \
			auto* _prof_args = std::get_if<TickArgs>(&(command).args);                         \
			g_profiler_physics_frame_id = _prof_args ? _prof_args->frame_id : UINT64_MAX;      \
		} else {                                                                               \
			g_profiler_physics_frame_id = UINT64_MAX;                                          \
		}                                                                                      \
		ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics,                       \
								   "engine.physics",                                           \
								   "CmdDequeue",                                               \
								   "command_type",                                             \
								   static_cast<int>((command).type));                           \
	} while (0)

#define ENGINE_PROFILE_PHYSICS_STEP(delta_time)           \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics,                   \
							   "engine.physics",                                       \
							   "PhysicsStep",                                          \
							   "delta_time",                                           \
							   static_cast<double>(delta_time),                         \
							   perfetto::TerminatingFlow::ProcessScoped(                \
								   g_profiler_physics_frame_id))

#define ENGINE_PROFILE_PHYSICS_COLLISION() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, "engine.physics", "Collision")

#define ENGINE_PROFILE_PHYSICS_TRANSFORM() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, "engine.physics", "Transform")

#define ENGINE_PROFILE_PHYSICS_DIFF() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, "engine.physics", "Diff")

#define ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Physics, "engine.physics", "ResultEnqueue")

// ── Entity system ───────────────────────────────────────────────────────

#define ENGINE_PROFILE_ENTITY_CREATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Entity, "engine.entity", "CreateEntity")

#define ENGINE_PROFILE_ENTITY_DESTROY() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Entity, "engine.entity", "DestroyEntity")

#define ENGINE_PROFILE_ENTITY_GET() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Entity, "engine.entity", "GetEntity")

#define ENGINE_PROFILE_ENTITY_ACTIVATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Entity, "engine.entity", "Activate")

#define ENGINE_PROFILE_ENTITY_SUSPEND() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Entity, "engine.entity", "Suspend")

// ── Space system ────────────────────────────────────────────────────────

#define ENGINE_PROFILE_SPACE_CREATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "CreateSpace")

#define ENGINE_PROFILE_SPACE_DESTROY() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "DestroySpace")

#define ENGINE_PROFILE_SPACE_GET() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "GetSpace")

#define ENGINE_PROFILE_SPACE_JOIN() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "OnPlayerJoin")

#define ENGINE_PROFILE_SPACE_LEAVE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "OnPlayerLeave")

#define ENGINE_PROFILE_SPACE_UPDATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "Update")

#define ENGINE_PROFILE_SPACE_LOAD_SCRIPTS() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "LoadScripts")

#define ENGINE_PROFILE_SPACE_MSG_SEND() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "SendMessage")

#define ENGINE_PROFILE_SPACE_MSG_PROCESS() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "ProcessPending")

#define ENGINE_PROFILE_SPACE_ROUTE_CONN() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "RouteConnection")

#define ENGINE_PROFILE_SPACE_ROUTE_MSG() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "RouteMessage")

#define ENGINE_PROFILE_SPACE_ROUTE_DISCONN() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Space, "engine.space", "RouteDisconnection")

// ── AOI system ──────────────────────────────────────────────────────────

#define ENGINE_PROFILE_AOI_REGISTER() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "RegisterEntity")

#define ENGINE_PROFILE_AOI_UNREGISTER() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "UnregisterEntity")

#define ENGINE_PROFILE_AOI_MOVE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "OnEntityMove")

#define ENGINE_PROFILE_AOI_VISIBILITY() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "RecomputeVisibility")

#define ENGINE_PROFILE_AOI_QUERY() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "QueryRadius")

#define ENGINE_PROFILE_AOI_GRID_INSERT() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "GridInsert")

#define ENGINE_PROFILE_AOI_GRID_UPDATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "GridUpdate")

#define ENGINE_PROFILE_AOI_GRID_REMOVE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Aoi, "engine.aoi", "GridRemove")

// ── Auth system ─────────────────────────────────────────────────────────

#define ENGINE_PROFILE_AUTH_AUTHENTICATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "Authenticate")

#define ENGINE_PROFILE_AUTH_VALIDATE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "ValidateSession")

#define ENGINE_PROFILE_AUTH_CREATE_SESSION() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "CreateSession")

#define ENGINE_PROFILE_AUTH_GET_SESSION() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "GetSession")

#define ENGINE_PROFILE_AUTH_REVOKE() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "RevokeSession")

#define ENGINE_PROFILE_AUTH_CLEANUP() \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Auth, "engine.auth", "CleanupExpired")

// ── Script system ───────────────────────────────────────────────────────

#define ENGINE_PROFILE_SCRIPT_DOFILE(path) \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Script, "engine.script", "DoFile", "path", path)

#define ENGINE_PROFILE_SCRIPT_EXPORT(name) \
	ENGINE_PROFILE_SCOPE_GROUP(::engine::ProfilerEventGroup::Script, "engine.script", "Export", "name", name)

#else  // ENGINE_PROFILER_ENABLED not defined

#define ENGINE_PROFILE_TICK() \
	do {                      \
	} while (0)
#define ENGINE_PROFILE_FRAME_BEGIN(frame_no, dt_ms) \
	do {                                            \
	} while (0)
#define ENGINE_PROFILE_FRAME_END(elapsed_ms) \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_SLOW_FRAME(elapsed_ms, threshold_ms) \
	do {                                                    \
	} while (0)
#define ENGINE_PROFILE_TIMER_UPDATE() \
	do {                              \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_TICK(frame_no) \
	do {                                      \
	} while (0)
#define ENGINE_PROFILE_SCRIPT_UPDATE() \
	do {                               \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_FETCH(frame_no) \
	do {                                       \
	} while (0)
#define ENGINE_PROFILE_SCRIPT_CALLBACK() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE(command) \
	do {                                            \
		(void) (command);                           \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_STEP(delta_time) \
	do {                                        \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_COLLISION() \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_TRANSFORM() \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_DIFF() \
	do {                              \
	} while (0)
#define ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE() \
	do {                                        \
	} while (0)
#define ENGINE_PROFILE_SCRIPT_DOFILE(path) \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_SCRIPT_EXPORT(name) \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_ENTITY_CREATE() \
	do {                               \
	} while (0)
#define ENGINE_PROFILE_ENTITY_DESTROY() \
	do {                                \
	} while (0)
#define ENGINE_PROFILE_ENTITY_GET() \
	do {                            \
	} while (0)
#define ENGINE_PROFILE_ENTITY_ACTIVATE() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_ENTITY_SUSPEND() \
	do {                                \
	} while (0)
#define ENGINE_PROFILE_SPACE_CREATE() \
	do {                              \
	} while (0)
#define ENGINE_PROFILE_SPACE_DESTROY() \
	do {                               \
	} while (0)
#define ENGINE_PROFILE_SPACE_GET() \
	do {                           \
	} while (0)
#define ENGINE_PROFILE_SPACE_JOIN() \
	do {                            \
	} while (0)
#define ENGINE_PROFILE_SPACE_LEAVE() \
	do {                             \
	} while (0)
#define ENGINE_PROFILE_SPACE_UPDATE() \
	do {                              \
	} while (0)
#define ENGINE_PROFILE_SPACE_LOAD_SCRIPTS() \
	do {                                    \
	} while (0)
#define ENGINE_PROFILE_SPACE_MSG_SEND() \
	do {                                \
	} while (0)
#define ENGINE_PROFILE_SPACE_MSG_PROCESS() \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_SPACE_ROUTE_CONN() \
	do {                                  \
	} while (0)
#define ENGINE_PROFILE_SPACE_ROUTE_MSG() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_SPACE_ROUTE_DISCONN() \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_AOI_REGISTER() \
	do {                              \
	} while (0)
#define ENGINE_PROFILE_AOI_UNREGISTER() \
	do {                                \
	} while (0)
#define ENGINE_PROFILE_AOI_MOVE() \
	do {                          \
	} while (0)
#define ENGINE_PROFILE_AOI_VISIBILITY() \
	do {                                \
	} while (0)
#define ENGINE_PROFILE_AOI_QUERY() \
	do {                           \
	} while (0)
#define ENGINE_PROFILE_AOI_GRID_INSERT() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_AOI_GRID_UPDATE() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_AOI_GRID_REMOVE() \
	do {                                 \
	} while (0)
#define ENGINE_PROFILE_AUTH_AUTHENTICATE() \
	do {                                   \
	} while (0)
#define ENGINE_PROFILE_AUTH_VALIDATE() \
	do {                               \
	} while (0)
#define ENGINE_PROFILE_AUTH_CREATE_SESSION() \
	do {                                     \
	} while (0)
#define ENGINE_PROFILE_AUTH_GET_SESSION() \
	do {                                  \
	} while (0)
#define ENGINE_PROFILE_AUTH_REVOKE() \
	do {                             \
	} while (0)
#define ENGINE_PROFILE_AUTH_CLEANUP() \
	do {                              \
	} while (0)

#endif	// ENGINE_PROFILER_ENABLED
