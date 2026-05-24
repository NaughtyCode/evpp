#pragma once

#include "engine/profiler/profiler_macros.h"

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
    ENGINE_PROFILE_SCOPE("engine", "Tick")

#define ENGINE_PROFILE_FRAME_BEGIN(frame_no, dt_ms) \
    do { \
        ENGINE_PROFILE_COUNTER_TRACK("engine", g_frame_no_track, \
            static_cast<int64_t>(frame_no)); \
        ENGINE_PROFILE_COUNTER_TRACK("engine", g_frame_dt_track, \
            static_cast<int64_t>(dt_ms)); \
        ENGINE_PROFILE_BEGIN("engine", "Frame", \
            "frame_no", static_cast<int64_t>(frame_no), \
            "dt_ms", static_cast<int64_t>(dt_ms)); \
    } while (0)

#define ENGINE_PROFILE_FRAME_END(elapsed_ms) \
    do { \
        ENGINE_PROFILE_COUNTER_TRACK("engine", g_frame_time_track, \
            static_cast<int64_t>(elapsed_ms)); \
        ENGINE_PROFILE_END("engine"); \
    } while (0)

#define ENGINE_PROFILE_SLOW_FRAME(elapsed_ms, threshold_ms) \
    do { \
        if ((elapsed_ms) > (threshold_ms)) { \
            ENGINE_PROFILE_INSTANT("engine", "SlowFrame"); \
        } \
    } while (0)

// ── Frame sub-stages ────────────────────────────────────────────────────

#define ENGINE_PROFILE_TIMER_UPDATE() \
    ENGINE_PROFILE_SCOPE("engine.timer", "TimerUpdate")

#define ENGINE_PROFILE_PHYSICS_TICK(frame_no) \
    ENGINE_PROFILE_SCOPE("engine.physics", "PhysicsTick", \
        "frame_no", static_cast<int64_t>(frame_no), \
        perfetto::Flow::ProcessScoped(static_cast<uint64_t>(frame_no)))

#define ENGINE_PROFILE_SCRIPT_UPDATE() \
    ENGINE_PROFILE_SCOPE("engine.script", "ScriptUpdate")

#define ENGINE_PROFILE_PHYSICS_FETCH(frame_no) \
    ENGINE_PROFILE_SCOPE("engine.physics", "PhysicsFetch", \
        "frame_no", static_cast<int64_t>(frame_no))

#define ENGINE_PROFILE_SCRIPT_CALLBACK() \
    ENGINE_PROFILE_SCOPE("engine.script", "ScriptCallback")

// ── Physics thread ──────────────────────────────────────────────────────

#define ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE() \
    do { \
        if (cmd.type == CommandType::Tick) { \
            auto* _prof_args = std::get_if<TickArgs>(&cmd.args); \
            g_profiler_physics_frame_id = _prof_args \
                ? _prof_args->frame_id : UINT64_MAX; \
        } else { \
            g_profiler_physics_frame_id = UINT64_MAX; \
        } \
        ENGINE_PROFILE_SCOPE("engine.physics", "CmdDequeue", \
            "command_type", static_cast<int>(cmd.type)); \
    } while (0)

#define ENGINE_PROFILE_PHYSICS_STEP(delta_time) \
    ENGINE_PROFILE_SCOPE("engine.physics", "PhysicsStep", \
        "delta_time", static_cast<double>(delta_time), \
        perfetto::TerminatingFlow::ProcessScoped( \
            g_profiler_physics_frame_id))

#define ENGINE_PROFILE_PHYSICS_COLLISION() \
    ENGINE_PROFILE_SCOPE("engine.physics", "Collision")

#define ENGINE_PROFILE_PHYSICS_TRANSFORM() \
    ENGINE_PROFILE_SCOPE("engine.physics", "Transform")

#define ENGINE_PROFILE_PHYSICS_DIFF() \
    ENGINE_PROFILE_SCOPE("engine.physics", "Diff")

#define ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE() \
    ENGINE_PROFILE_SCOPE("engine.physics", "ResultEnqueue")

// ── Script system ───────────────────────────────────────────────────────

#define ENGINE_PROFILE_SCRIPT_DOFILE(path) \
    ENGINE_PROFILE_SCOPE("engine.script", "DoFile", "path", path)

#define ENGINE_PROFILE_SCRIPT_EXPORT(name) \
    ENGINE_PROFILE_SCOPE("engine.script", "Export", "name", name)

#else // ENGINE_PROFILER_ENABLED not defined

#define ENGINE_PROFILE_TICK()                                     do {} while (0)
#define ENGINE_PROFILE_FRAME_BEGIN(frame_no, dt_ms)               do {} while (0)
#define ENGINE_PROFILE_FRAME_END(elapsed_ms)                      do {} while (0)
#define ENGINE_PROFILE_SLOW_FRAME(elapsed_ms, threshold_ms)       do {} while (0)
#define ENGINE_PROFILE_TIMER_UPDATE()                             do {} while (0)
#define ENGINE_PROFILE_PHYSICS_TICK(frame_no)                     do {} while (0)
#define ENGINE_PROFILE_SCRIPT_UPDATE()                            do {} while (0)
#define ENGINE_PROFILE_PHYSICS_FETCH(frame_no)                    do {} while (0)
#define ENGINE_PROFILE_SCRIPT_CALLBACK()                          do {} while (0)
#define ENGINE_PROFILE_PHYSICS_CMD_DEQUEUE()                      do {} while (0)
#define ENGINE_PROFILE_PHYSICS_STEP(delta_time)                   do {} while (0)
#define ENGINE_PROFILE_PHYSICS_COLLISION()                        do {} while (0)
#define ENGINE_PROFILE_PHYSICS_TRANSFORM()                        do {} while (0)
#define ENGINE_PROFILE_PHYSICS_DIFF()                             do {} while (0)
#define ENGINE_PROFILE_PHYSICS_RESULT_ENQUEUE()                   do {} while (0)
#define ENGINE_PROFILE_SCRIPT_DOFILE(path)                        do {} while (0)
#define ENGINE_PROFILE_SCRIPT_EXPORT(name)                        do {} while (0)

#endif // ENGINE_PROFILER_ENABLED
