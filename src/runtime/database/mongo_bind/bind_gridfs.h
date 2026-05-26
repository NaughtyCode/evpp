#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoGridFsFileOptsMeta(lua_State* L);
void RegisterMongoGridFsFileMeta(lua_State* L);
void RegisterMongoGridFsFileListMeta(lua_State* L);
void RegisterMongoGridFsMeta(lua_State* L);
void RegisterMongoGridFsBucketMeta(lua_State* L);

const luaL_Reg* GetMongoGridFsFileOptsLib();
const luaL_Reg* GetMongoGridFsFileLib();
const luaL_Reg* GetMongoGridFsFileListLib();
const luaL_Reg* GetMongoGridFsLib();
const luaL_Reg* GetMongoGridFsBucketLib();

} // namespace script
} // namespace engine

#endif
