#include "runtime/script/rpc_bind.h"

#include <memory>
#include <string>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/rpc/rpc_client.h"
#include "runtime/rpc/rpc_server.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

std::unique_ptr<rpc::RpcServer> g_rpc_server;
std::unique_ptr<rpc::RpcClient> g_rpc_client;

// ── RPC Server ─────────────────────────────────────────────────────────

// rpc.start_server()
int l_rpc_start_server(lua_State* L) {
	g_rpc_server = std::make_unique<rpc::RpcServer>();
	lua_pushboolean(L, 1);
	return 1;
}

// rpc.register_service(service_name, callback)
// callback(service, method, body) → response_body
int l_rpc_register_service(lua_State* L) {
	if (!g_rpc_server) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "RPC server not started");
		return 2;
	}

	const char* service_name = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	g_rpc_server->RegisterService(service_name,
		[cb_ref](const std::string& method, const std::string& body) -> std::string {
			lua_State* L = engine::Engine::Instance().GetScriptVM().GetState();
			if (!L) return "{}";

			lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
			lua_pushstring(L, method.c_str());
			lua_pushstring(L, body.c_str());

			std::string result = "{}";
			if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
				if (lua_isstring(L, -1)) {
					result = lua_tostring(L, -1);
				}
			}
			lua_pop(L, 1);
			return result;
		});

	lua_pushboolean(L, 1);
	return 1;
}

// rpc.stop_server()
int l_rpc_stop_server(lua_State* L) {
	g_rpc_server.reset();
	lua_pushboolean(L, 1);
	return 1;
}

// ── RPC Client ─────────────────────────────────────────────────────────

// rpc.start_client()
int l_rpc_start_client(lua_State* L) {
	g_rpc_client = std::make_unique<rpc::RpcClient>();
	lua_pushboolean(L, 1);
	return 1;
}

// rpc.call(service, method, args_json, timeout_ms) → response_json or nil, err
int l_rpc_call_sync(lua_State* L) {
	if (!g_rpc_client) {
		lua_pushnil(L);
		lua_pushstring(L, "RPC client not started");
		return 2;
	}

	const char* service = luaL_checkstring(L, 1);
	const char* method = luaL_checkstring(L, 2);
	const char* args = luaL_optstring(L, 3, "{}");
	int timeout_ms = static_cast<int>(luaL_optinteger(L, 4, 5000));

	auto resp = g_rpc_client->CallSync(service, method, args, timeout_ms);

	if (resp.success) {
		lua_pushstring(L, resp.body.c_str());
		lua_pushnil(L);
	} else {
		lua_pushnil(L);
		lua_pushstring(L, resp.error_message.c_str());
	}
	return 2;
}

// rpc.stop_client()
int l_rpc_stop_client(lua_State* L) {
	g_rpc_client.reset();
	lua_pushboolean(L, 1);
	return 1;
}

static const luaL_Reg kRpcFuncs[] = {
	{"start_server",      l_rpc_start_server},
	{"register_service",  l_rpc_register_service},
	{"stop_server",       l_rpc_stop_server},
	{"start_client",      l_rpc_start_client},
	{"call",              l_rpc_call_sync},
	{"stop_client",       l_rpc_stop_client},
	{nullptr, nullptr}
};

}  // namespace

void ExportRpc(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);
	for (const luaL_Reg* r = kRpcFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "rpc");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RPC API exported to Lua");
}

}  // namespace script
}  // namespace engine
