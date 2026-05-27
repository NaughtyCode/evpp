#pragma once

#if defined(ENGINE_MONGODB_ENABLED)

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

const luaL_Reg* GetOidLib();

}  // namespace script
}  // namespace engine

#endif
