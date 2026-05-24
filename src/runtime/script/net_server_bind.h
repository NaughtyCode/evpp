#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Push the net.server library table onto the Lua stack.
// Table contains: listen, send, close_conn, stop, set_on_message,
//                 set_on_close, set_on_connect, set_on_disconnect
ENGINE_API void PushServerLibrary(lua_State* L);

// Stop all active servers and release Lua callback references.
ENGINE_API void ShutdownServerBindings();

} // namespace script
} // namespace engine
