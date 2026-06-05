#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include "runtime/physics/physics_log.h"

namespace engine {
namespace physics_bindings {

namespace {

quill::Logger* GetPhysicsLoggerFromState(lua_State* L) {
	BindingContext ctx = GetContext(L);
	if (ctx.thread) {
		if (auto* logger = ctx.thread->GetLogger()) {
			return logger;
		}
	}
	return ctx.system ? ctx.system->GetPhysicsLogger() : nullptr;
}

#define PHYSICS_LUA_LOG_CALL(name, macro)                 \
	int LuaLog##name(lua_State* L) {                      \
		const char* msg = luaL_checkstring(L, 1);         \
		if (auto* logger = GetPhysicsLoggerFromState(L)) { \
			macro(logger, "[physics_lua] {}", msg);       \
		}                                                 \
		return 0;                                         \
	}

PHYSICS_LUA_LOG_CALL(Trace, PHYSICS_LOG_TRACE)
PHYSICS_LUA_LOG_CALL(Debug, PHYSICS_LOG_DEBUG)
PHYSICS_LUA_LOG_CALL(Info, PHYSICS_LOG_INFO)
PHYSICS_LUA_LOG_CALL(Warn, PHYSICS_LOG_WARN)
PHYSICS_LUA_LOG_CALL(Error, PHYSICS_LOG_ERROR)
PHYSICS_LUA_LOG_CALL(Fatal, PHYSICS_LOG_CRITICAL)

#undef PHYSICS_LUA_LOG_CALL

const luaL_Reg kPhysicsLogGlobals[] = {{"log_trace", LuaLogTrace},
									   {"log_debug", LuaLogDebug},
									   {"log_info", LuaLogInfo},
									   {"log_warn", LuaLogWarn},
									   {"log_error", LuaLogError},
									   {"log_fatal", LuaLogFatal},
									   {nullptr, nullptr}};

const luaL_Reg kPhysicsLogModule[] = {{"log_trace", LuaLogTrace},
									  {"log_debug", LuaLogDebug},
									  {"log_info", LuaLogInfo},
									  {"log_warn", LuaLogWarn},
									  {"log_error", LuaLogError},
									  {"log_fatal", LuaLogFatal},
									  {nullptr, nullptr}};

}  // namespace

void RegisterLogGlobals(ScriptVM& vm) {
	vm.RegisterFunctions(kPhysicsLogGlobals);
}

void RegisterLogModuleBindings(lua_State* L) {
	luaL_setfuncs(L, kPhysicsLogModule, 0);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
