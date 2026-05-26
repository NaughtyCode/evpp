#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/core/engine_api.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

void RegisterMongoHostListMeta(lua_State* L);
const luaL_Reg* GetMongoHostListLib();

} // namespace script
} // namespace engine

#endif
