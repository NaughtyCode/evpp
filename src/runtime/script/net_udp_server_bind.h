#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Push the net.udp_server library table onto the Lua stack.
// Table contains: listen, stop, pause, continue, is_running, set_on_message
ENGINE_API void PushUdpServerLibrary(lua_State* L);

// Stop all active UDP servers and release Lua callback references.
ENGINE_API void ShutdownUdpServerBindings();

} // namespace script
} // namespace engine
