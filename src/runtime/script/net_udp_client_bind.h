#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Register the "net.udp_client.instance" metatable (instance methods + __gc).
// Must be called once before PushUdpClientLibrary.
ENGINE_API void RegisterUdpClientMetaTable(lua_State* L);

// Push the net.udp_client library table onto the Lua stack.
// Table contains: connect, do_request, send_to
ENGINE_API void PushUdpClientLibrary(lua_State* L);

}  // namespace script
}  // namespace engine
