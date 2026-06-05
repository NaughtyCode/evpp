#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register the "net.udp_server.instance" metatable (instance methods + __gc).
// Must be called once before PushUdpServerLibrary.
CLOUD_ENGINE_API void RegisterUdpServerMetaTable(lua_State* L);

// Push the net.udp_server library table onto the Lua stack.
// Table contains: listen
CLOUD_ENGINE_API void PushUdpServerLibrary(lua_State* L);

// Stop all active UDP servers and release Lua callback references.
CLOUD_ENGINE_API void ShutdownUdpServerBindings();

}  // namespace script
}  // namespace engine
