#pragma once

#include "runtime/core/engine_api.h"

struct lua_State;

namespace engine {
namespace script {

// Push the net.http library table onto the Lua stack.
// Table contains: get, post
ENGINE_API void PushHttpLibrary(lua_State* L);

// Prevent in-flight HTTP callbacks from touching a freed Lua state,
// then release all pending HTTP callback registry references.
ENGINE_API void ShutdownHttpBindings();

}  // namespace script
}  // namespace engine
