#include "runtime/database/redis/bind/redis_bind.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/database/redis/redis_client.h"
#include "runtime/database/redis/redis_client_internal.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

using redis::RedisClient;
using redis::RedisCommandOptions;
using redis::RedisResult;
using redis::RedisResultStatusToString;
using redis::RedisSubmitResult;
using redis::RedisValue;
using redis::RedisValueType;
using redis::RedisValueTypeToString;

struct RedisContextUserdata {
	RedisLuaBindingContext* context = nullptr;
};

RedisLuaBindingContext* GetContext(lua_State* L) {
	return static_cast<RedisLuaBindingContext*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int l_context_gc(lua_State* L) {
	auto* userdata = static_cast<RedisContextUserdata*>(
		luaL_checkudata(L, 1, "engine.redis.context"));
	delete userdata->context;
	userdata->context = nullptr;
	return 0;
}

bool ReadStringArray(lua_State* L, int index, std::vector<std::string>& out) {
	if (!lua_istable(L, index)) {
		return false;
	}
	index = lua_absindex(L, index);
	const lua_Integer len = luaL_len(L, index);
	if (len < 0) {
		return false;
	}
	out.clear();
	out.reserve(static_cast<size_t>(len));
	for (lua_Integer i = 1; i <= len; ++i) {
		lua_rawgeti(L, index, i);
		size_t value_len = 0;
		const char* value = lua_tolstring(L, -1, &value_len);
		if (!value) {
			lua_pop(L, 1);
			return false;
		}
		out.emplace_back(value, value_len);
		lua_pop(L, 1);
	}
	return true;
}

RedisCommandOptions ReadOptions(lua_State* L, int index) {
	RedisCommandOptions options;
	if (!lua_istable(L, index)) {
		return options;
	}
	index = lua_absindex(L, index);

	lua_getfield(L, index, "timeout_ms");
	if (lua_isinteger(L, -1)) {
		options.timeout_ms = static_cast<int>(lua_tointeger(L, -1));
	}
	lua_pop(L, 1);

	lua_getfield(L, index, "routing_key");
	if (lua_isstring(L, -1)) {
		size_t len = 0;
		const char* value = lua_tolstring(L, -1, &len);
		options.routing_key.assign(value, len);
	}
	lua_pop(L, 1);

	return options;
}

void PushRedisValue(lua_State* L, const RedisValue& value) {
	lua_createtable(L, 0, 3);
	lua_pushstring(L, RedisValueTypeToString(value.type));
	lua_setfield(L, -2, "type");

	switch (value.type) {
	case RedisValueType::kNull:
		lua_pushnil(L);
		lua_setfield(L, -2, "value");
		break;
	case RedisValueType::kString:
	case RedisValueType::kStatus:
	case RedisValueType::kError:
	case RedisValueType::kBigNumber:
	case RedisValueType::kVerbatimString:
		lua_pushlstring(L, value.string_value.data(), value.string_value.size());
		lua_setfield(L, -2, "value");
		break;
	case RedisValueType::kInteger:
		lua_pushinteger(L, static_cast<lua_Integer>(value.integer_value));
		lua_setfield(L, -2, "value");
		break;
	case RedisValueType::kDouble:
		lua_pushnumber(L, static_cast<lua_Number>(value.double_value));
		lua_setfield(L, -2, "value");
		break;
	case RedisValueType::kBool:
		lua_pushboolean(L, value.bool_value ? 1 : 0);
		lua_setfield(L, -2, "value");
		break;
	case RedisValueType::kArray:
	case RedisValueType::kMap:
	case RedisValueType::kSet:
	case RedisValueType::kPush:
	case RedisValueType::kAttribute:
		lua_createtable(L, static_cast<int>(value.array_value.size()), 0);
		for (size_t i = 0; i < value.array_value.size(); ++i) {
			PushRedisValue(L, value.array_value[i]);
			lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
		}
		lua_setfield(L, -2, "value");
		break;
	default:
		lua_pushnil(L);
		lua_setfield(L, -2, "value");
		break;
	}
}

int PushRedisResult(lua_State* L, const RedisResult& result) {
	lua_createtable(L, 0, 5);
	lua_pushstring(L, RedisResultStatusToString(result.status));
	lua_setfield(L, -2, "status");
	lua_pushboolean(L, result.status == redis::RedisResultStatus::kOk ? 1 : 0);
	lua_setfield(L, -2, "ok");
	lua_pushinteger(L, static_cast<lua_Integer>(result.request_id));
	lua_setfield(L, -2, "request_id");
	if (!result.error.empty()) {
		lua_pushlstring(L, result.error.data(), result.error.size());
		lua_setfield(L, -2, "error");
	}
	PushRedisValue(L, result.value);
	lua_setfield(L, -2, "value");
	return 1;
}

auto BuildCompletion(std::shared_ptr<AsyncResultDispatcher> dispatcher,
					 AsyncCallbackId callback_id) {
	return [dispatcher = std::move(dispatcher), callback_id](RedisResult result) mutable {
		if (!dispatcher) return;
		dispatcher->EnqueueLuaCallback(callback_id,
			[result = std::move(result)](lua_State* L) mutable {
				return PushRedisResult(L, result);
			});
	};
}

int l_redis_command(lua_State* L) {
	auto* context = GetContext(L);
	if (!context || !context->dispatcher) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "redis binding context is not available");
		return 2;
	}
	std::vector<std::string> argv;
	if (!ReadStringArray(L, 1, argv)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "redis.command expects argv string array");
		return 2;
	}
	luaL_checktype(L, 2, LUA_TFUNCTION);
	auto options = ReadOptions(L, 3);

	const AsyncCallbackId callback_id = context->dispatcher->RegisterLuaCallback(L, 2);
	if (callback_id == 0) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "failed to register redis callback");
		return 2;
	}

	RedisSubmitResult result = redis::internal::RedisClientAccess::Command(
		std::move(argv),
		BuildCompletion(context->dispatcher, callback_id),
		std::move(options),
		context->preferred_worker_index);
	if (!result.accepted()) {
		context->dispatcher->ReleaseLuaCallback(callback_id);
		lua_pushboolean(L, 0);
		lua_pushlstring(L, result.error.data(), result.error.size());
		return 2;
	}

	lua_pushboolean(L, 1);
	lua_pushinteger(L, static_cast<lua_Integer>(result.request_id));
	return 2;
}

int l_redis_eval(lua_State* L) {
	auto* context = GetContext(L);
	if (!context || !context->dispatcher) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "redis binding context is not available");
		return 2;
	}
	size_t script_len = 0;
	const char* script = luaL_checklstring(L, 1, &script_len);
	std::vector<std::string> keys;
	std::vector<std::string> args;
	if (!ReadStringArray(L, 2, keys)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "redis.eval expects keys string array");
		return 2;
	}
	if (!ReadStringArray(L, 3, args)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "redis.eval expects args string array");
		return 2;
	}
	luaL_checktype(L, 4, LUA_TFUNCTION);
	auto options = ReadOptions(L, 5);

	const AsyncCallbackId callback_id = context->dispatcher->RegisterLuaCallback(L, 4);
	if (callback_id == 0) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "failed to register redis callback");
		return 2;
	}

	RedisSubmitResult result = redis::internal::RedisClientAccess::Eval(
		std::string(script, script_len),
		std::move(keys),
		std::move(args),
		BuildCompletion(context->dispatcher, callback_id),
		std::move(options),
		context->preferred_worker_index);
	if (!result.accepted()) {
		context->dispatcher->ReleaseLuaCallback(callback_id);
		lua_pushboolean(L, 0);
		lua_pushlstring(L, result.error.data(), result.error.size());
		return 2;
	}

	lua_pushboolean(L, 1);
	lua_pushinteger(L, static_cast<lua_Integer>(result.request_id));
	return 2;
}

int l_redis_is_running(lua_State* L) {
	lua_pushboolean(L, RedisClient::Instance().IsRunning() ? 1 : 0);
	return 1;
}

int l_redis_is_healthy(lua_State* L) {
	lua_pushboolean(L, RedisClient::Instance().IsHealthy() ? 1 : 0);
	return 1;
}

int l_redis_dispatch(lua_State* L) {
	auto* context = GetContext(L);
	if (!context || !context->dispatcher) {
		lua_pushinteger(L, 0);
		return 1;
	}
	size_t max_count = context->dispatch_batch_size;
	if (lua_isinteger(L, 1)) {
		max_count = static_cast<size_t>(lua_tointeger(L, 1));
	}
	lua_pushinteger(L, static_cast<lua_Integer>(context->dispatcher->Dispatch(max_count)));
	return 1;
}

void EnsureContextMetatable(lua_State* L) {
	if (luaL_newmetatable(L, "engine.redis.context")) {
		lua_pushcfunction(L, l_context_gc);
		lua_setfield(L, -2, "__gc");
	}
	lua_pop(L, 1);
}

}  // namespace

void ExportRedis(ScriptVM& vm, RedisLuaBindingContext context) {
	if (!context.dispatcher) {
		context.dispatcher = vm.GetAsyncDispatcher();
	}
	lua_State* L = vm.GetState();
	if (!L) return;

	EnsureContextMetatable(L);
	auto* heap_context = new RedisLuaBindingContext(std::move(context));

	lua_createtable(L, 0, 5);
	lua_pushlightuserdata(L, heap_context);
	lua_pushcclosure(L, l_redis_command, 1);
	lua_setfield(L, -2, "command");
	lua_pushlightuserdata(L, heap_context);
	lua_pushcclosure(L, l_redis_eval, 1);
	lua_setfield(L, -2, "eval");
	lua_pushlightuserdata(L, heap_context);
	lua_pushcclosure(L, l_redis_is_running, 1);
	lua_setfield(L, -2, "is_running");
	lua_pushlightuserdata(L, heap_context);
	lua_pushcclosure(L, l_redis_is_healthy, 1);
	lua_setfield(L, -2, "is_healthy");
	lua_pushlightuserdata(L, heap_context);
	lua_pushcclosure(L, l_redis_dispatch, 1);
	lua_setfield(L, -2, "dispatch");

	auto* userdata = static_cast<RedisContextUserdata*>(
		lua_newuserdatauv(L, sizeof(RedisContextUserdata), 0));
	userdata->context = heap_context;
	luaL_getmetatable(L, "engine.redis.context");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "_context");

	lua_pushvalue(L, -1);
	lua_setglobal(L, "redis");
	lua_getglobal(L, "package");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "loaded");
		if (lua_istable(L, -1)) {
			lua_pushvalue(L, -3);
			lua_setfield(L, -2, "redis");
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_pop(L, 1);

	ENGINE_LOG_INFO(GetLogger(), "ScriptBind: redis module exported");
}

void ShutdownRedisBindings(ScriptVM& vm) {
	lua_State* L = vm.GetState();
	if (!L) return;
	lua_pushnil(L);
	lua_setglobal(L, "redis");
	lua_getglobal(L, "package");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "loaded");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			lua_setfield(L, -2, "redis");
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

}  // namespace script
}  // namespace engine
