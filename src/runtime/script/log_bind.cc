#include "runtime/script/log_bind.h"

#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {

namespace {

#define LUA_LOG_CALL(name, macro)                               \
    int l_log_##name(lua_State* L) {                             \
        const char* msg = luaL_checkstring(L, 1);                \
        auto* logger = GetLogger();                              \
        if (logger) { macro(logger, "[lua] {}", msg); }          \
        return 0;                                                \
    }

LUA_LOG_CALL(trace, ENGINE_LOG_TRACE)
LUA_LOG_CALL(debug, ENGINE_LOG_DEBUG)
LUA_LOG_CALL(info,  ENGINE_LOG_INFO)
LUA_LOG_CALL(warn,  ENGINE_LOG_WARN)
LUA_LOG_CALL(error, ENGINE_LOG_ERROR)
LUA_LOG_CALL(fatal, ENGINE_LOG_CRITICAL)

#undef LUA_LOG_CALL

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
