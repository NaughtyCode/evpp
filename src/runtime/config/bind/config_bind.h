#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {

class ScriptVM;

namespace script {

// Export the "config" Lua module with config.get/get_module/on_change/flush_changes
// functions. Registered as a global table so Lua can call config.get(path), etc.
CLOUD_ENGINE_API void ExportConfigBindings(ScriptVM& vm);

// Flush pending config-change events for the given Lua state.
// Must be called from the thread that owns `L` (typically the main/event-loop
// thread). ConfigManager reload callbacks enqueue events from any thread
// (including the FileWatcher thread); this function dispatches them to the
// registered Lua on_change handlers.
// Returns the number of callbacks invoked.
CLOUD_ENGINE_API int FlushConfigCallbacks(::lua_State* L);

// Shut down config bindings for the given Lua state.
// - Unregisters all config.on_change callbacks from ConfigManager.
// - Releases Lua function registry references.
// - Drops any pending config-change events buffered for this state.
CLOUD_ENGINE_API void ShutdownConfigBindings(::lua_State* L);

}  // namespace script
}  // namespace engine
