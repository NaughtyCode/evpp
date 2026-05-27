#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoAutoEncryptionOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptRangeOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptTextPrefixOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptTextSuffixOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptTextSubstringOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionEncryptTextOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionDatakeyOptsMeta(lua_State* L);
void RegisterMongoClientEncryptionRewrapManyDatakeyResultMeta(lua_State* L);
void RegisterMongoClientEncryptionMeta(lua_State* L);

const luaL_Reg* GetMongoAutoEncryptionOptsLib();
const luaL_Reg* GetMongoClientEncryptionOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptRangeOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptTextPrefixOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptTextSuffixOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptTextSubstringOptsLib();
const luaL_Reg* GetMongoClientEncryptionEncryptTextOptsLib();
const luaL_Reg* GetMongoClientEncryptionDatakeyOptsLib();
const luaL_Reg* GetMongoClientEncryptionRewrapManyDatakeyResultLib();
const luaL_Reg* GetMongoClientEncryptionLib();

} // namespace script
} // namespace engine

#endif
