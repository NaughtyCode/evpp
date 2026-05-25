#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register the "net.kcp_client.instance" metatable (instance methods + __gc).
// Must be called once before PushKcpClientLibrary.
ENGINE_API void RegisterKcpClientMetaTable(lua_State* L);

// Push the net.kcp_client library table onto the Lua stack.
// Table contains: connect, do_request, new
ENGINE_API void PushKcpClientLibrary(lua_State* L);

} // namespace script
} // namespace engine
