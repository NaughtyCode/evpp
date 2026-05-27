#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterBsonContextMeta(lua_State* L);
const luaL_Reg* GetBsonContextLib();

void RegisterBsonStringMeta(lua_State* L);
const luaL_Reg* GetBsonStringLib();

void RegisterBsonJsonReaderMeta(lua_State* L);
const luaL_Reg* GetBsonJsonReaderLib();

void RegisterBsonJsonDataReaderMeta(lua_State* L);
const luaL_Reg* GetBsonJsonDataReaderLib();

void RegisterBsonReaderMeta(lua_State* L);
const luaL_Reg* GetBsonReaderLib();

void RegisterBsonWriterMeta(lua_State* L);
const luaL_Reg* GetBsonWriterLib();

void RegisterBsonJsonOptsMeta(lua_State* L);
const luaL_Reg* GetBsonJsonOptsLib();

void RegisterBsonValueMeta(lua_State* L);
const luaL_Reg* GetBsonValueLib();

const luaL_Reg* GetBsonExtLib();

} // namespace script
} // namespace engine

#endif
