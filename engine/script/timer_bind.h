#pragma once

#include "engine/engine_export.h"

namespace engine {

class ScriptVM;

namespace script {

// Export the "timer" module to Lua with the following API:
//
//   timer.timeout(delay_ms, callback)  → timer_id (one-shot)
//   timer.interval(period_ms, callback) → timer_id (repeating)
//   timer.cancel(timer_id)             → true/false
//
// Callbacks are Lua functions that receive no arguments.
// Returns 0 on failure (Lua will see nil / false).
ENGINE_API void ExportTimer(ScriptVM& vm);

// Cancel all Lua-owned timers and release Lua function references.
ENGINE_API void ShutdownTimerBindings();

} // namespace script
} // namespace engine
