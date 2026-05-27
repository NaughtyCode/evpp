#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register the "net.client.instance" metatable (instance methods + __gc).
// Must be called once before PushClientLibrary.
ENGINE_API void RegisterClientMetaTable(lua_State* L);

// Push the net.client library table onto the Lua stack.
// Table contains: connect
ENGINE_API void PushClientLibrary(lua_State* L);

// Close all active TCP client connections, cancel callbacks, and free
// context objects. Called during engine shutdown.
ENGINE_API void ShutdownClientBindings();

}  // namespace script
}  // namespace engine
