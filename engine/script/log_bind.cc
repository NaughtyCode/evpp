#include "engine/script/log_bind.h"

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/vm/vm.h"

namespace engine {
namespace script {

namespace {

int l_log_trace(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_TRACE(logger, "[lua] {}", msg);
    return 0;
}

int l_log_debug(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_DEBUG(logger, "[lua] {}", msg);
    return 0;
}

int l_log_info(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "[lua] {}", msg);
    return 0;
}

int l_log_warn(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_WARN(logger, "[lua] {}", msg);
    return 0;
}

int l_log_error(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_ERROR(logger, "[lua] {}", msg);
    return 0;
}

int l_log_fatal(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    auto* logger = GetLogger();
    ENGINE_LOG_FATAL(logger, "[lua] {}", msg);
    return 0;
}

const luaL_Reg kLogFunctions[] = {
    {"log_trace", l_log_trace},
    {"log_debug", l_log_debug},
    {"log_info",  l_log_info},
    {"log_warn",  l_log_warn},
    {"log_error", l_log_error},
    {"log_fatal", l_log_fatal},
    {nullptr, nullptr},
};

} // namespace

void ExportLog(ScriptVM& vm) {
    vm.RegisterFunctions(kLogFunctions);

    auto* logger = GetLogger();
    ENGINE_LOG_INFO(logger, "ScriptBind: log functions exported "
                    "(log_trace/debug/info/warn/error/fatal)");
}

} // namespace script
} // namespace engine
