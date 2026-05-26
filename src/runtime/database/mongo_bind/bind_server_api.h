#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/core/engine_api.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoServerApiMeta(lua_State* L);
const luaL_Reg* GetMongoServerApiLib();

} // namespace script
} // namespace engine

#endif
