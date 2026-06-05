#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterBsonVectorInt8ConstViewMeta(lua_State* L);
void RegisterBsonVectorInt8ViewMeta(lua_State* L);
void RegisterBsonVectorFloat32ConstViewMeta(lua_State* L);
void RegisterBsonVectorFloat32ViewMeta(lua_State* L);
void RegisterBsonVectorPackedBitConstViewMeta(lua_State* L);
void RegisterBsonVectorPackedBitViewMeta(lua_State* L);

const luaL_Reg* GetBsonVectorInt8ConstViewLib();
const luaL_Reg* GetBsonVectorInt8ViewLib();
const luaL_Reg* GetBsonVectorFloat32ConstViewLib();
const luaL_Reg* GetBsonVectorFloat32ViewLib();
const luaL_Reg* GetBsonVectorPackedBitConstViewLib();
const luaL_Reg* GetBsonVectorPackedBitViewLib();

}  // namespace script
}  // namespace engine

#endif
