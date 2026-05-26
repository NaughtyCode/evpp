#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoWriteConcernMeta(lua_State* L);
const luaL_Reg* GetMongoWriteConcernLib();

} // namespace script
} // namespace engine

#endif
