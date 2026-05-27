#pragma once

#include "runtime/core/engine_api.h"

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
// Timer state is per-VM — each ScriptVM has an independent timer table.
ENGINE_API void ExportTimer(ScriptVM& vm);

// Cancel all Lua-owned timers for a specific VM and release its Lua
// function references.  Must be called before the ScriptVM is destroyed.
ENGINE_API void ShutdownTimerBindings(ScriptVM& vm);

}  // namespace script
}  // namespace engine
