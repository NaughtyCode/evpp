#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoOptionalMeta(lua_State* L);
const luaL_Reg* GetMongoOptionalLib();
const luaL_Reg* GetMongoMiscLib();

} // namespace script
} // namespace engine

#endif
