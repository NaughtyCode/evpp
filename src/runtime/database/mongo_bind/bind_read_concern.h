#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoReadConcernMeta(lua_State* L);
const luaL_Reg* GetMongoReadConcernLib();

} // namespace script
} // namespace engine

#endif
