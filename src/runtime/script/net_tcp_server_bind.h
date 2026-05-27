#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register metatables for server and connection instances (methods + __gc).
// Must be called once before PushServerLibrary.
ENGINE_API void RegisterServerMetaTable(lua_State* L);
ENGINE_API void RegisterConnMetaTable(lua_State* L);

// Push the net.server library table onto the Lua stack.
// Table contains: listen
ENGINE_API void PushServerLibrary(lua_State* L);

// Stop all active TCP servers and release Lua callback references.
ENGINE_API void ShutdownServerBindings();

} // namespace script
} // namespace engine
