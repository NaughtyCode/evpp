#include "runtime/script/bind_util.h"

#include <cstdarg>
#include <cstdio>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace script {

int LuaError(lua_State* L, const char* fmt, ...) {
	ENGINE_PROFILE_SCOPE("engine.script", "LuaError");
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lua_pushstring(L, buf);
    return lua_error(L);
}

}  // namespace script
}  // namespace engine
