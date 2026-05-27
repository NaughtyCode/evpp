#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoServerDescriptionMeta(lua_State* L);
void RegisterMongoTopologyDescriptionMeta(lua_State* L);

const luaL_Reg* GetMongoServerDescriptionLib();
const luaL_Reg* GetMongoTopologyDescriptionLib();

} // namespace script
} // namespace engine

#endif
