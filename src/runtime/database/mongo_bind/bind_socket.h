#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoSocketMeta(lua_State* L);
const luaL_Reg* GetMongoSocketLib();

} // namespace script
} // namespace engine

#endif
