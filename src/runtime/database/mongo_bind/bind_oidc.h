#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoOidcCredentialMeta(lua_State* L);
void RegisterMongoOidcCallbackParamsMeta(lua_State* L);
void RegisterMongoOidcCallbackMeta(lua_State* L);

const luaL_Reg* GetMongoOidcCredentialLib();
const luaL_Reg* GetMongoOidcCallbackParamsLib();
const luaL_Reg* GetMongoOidcCallbackLib();

} // namespace script
} // namespace engine

#endif
