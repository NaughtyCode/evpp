#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register the "net.kcp_server.instance" metatable (instance methods + __gc).
// Must be called once before PushKcpServerLibrary.
ENGINE_API void RegisterKcpServerMetaTable(lua_State* L);

// Push the net.kcp_server library table onto the Lua stack.
// Table contains: listen
ENGINE_API void PushKcpServerLibrary(lua_State* L);

// Stop all active KCP servers and release their Lua references.
// Called during engine shutdown before the Lua VM is destroyed.
ENGINE_API void ShutdownKcpServerBindings();

} // namespace script
} // namespace engine
