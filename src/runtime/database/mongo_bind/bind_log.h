#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoStructuredLogOptsMeta(lua_State* L);
void RegisterMongoStructuredLogEntryMeta(lua_State* L);

const luaL_Reg* GetMongoStructuredLogOptsLib();
const luaL_Reg* GetMongoStructuredLogEntryLib();

} // namespace script
} // namespace engine

#endif
