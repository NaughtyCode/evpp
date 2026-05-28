#include "runtime/script/rpc_bind.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/rpc/rpc_client.h"
#include "runtime/rpc/rpc_server.h"
#include "runtime/script/bind_util.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

// ── Metatable names ─────────────────────────────────────────────────────

const char* kServerMetaName = "rpc.server.instance";
const char* kClientMetaName = "rpc.client.instance";

// ── Pending request (deferred execution queue) ──────────────────────────
//
// RpcServer::RegisterService stores a handler that may be called from an
// arbitrary (network / transport) thread via HandleRequest().  Lua must
// only be touched from the VM thread, so the handler enqueues a
// PendingRpcCall and blocks on its std::future.  UpdateRpcBindings()
// drains the queue on the main thread and fulfills the promise.

struct PendingRpcCall {
	std::string service;
	std::string method;
	std::string body;
	std::promise<std::string> promise;
};

// ── Server context ──────────────────────────────────────────────────────

struct RpcServerCtx {
	std::unique_ptr<rpc::RpcServer> server;
	lua_State* L = nullptr;
	int instance_ref = LUA_NOREF;

	// service_name → Lua callback registry ref
	std::unordered_map<std::string, int> service_callbacks;

	// Deferred execution queue
	std::mutex queue_mutex;
	std::vector<std::unique_ptr<PendingRpcCall>> pending;
	std::atomic<bool> alive{true};
	bool disposed = false;
};

// ── Client context ──────────────────────────────────────────────────────

struct RpcClientCtx {
	std::unique_ptr<rpc::RpcClient> client;
	lua_State* L = nullptr;
	int instance_ref = LUA_NOREF;
	bool disposed = false;
};

// ── Per-VM state ────────────────────────────────────────────────────────

struct RpcBindState {
	std::unordered_set<RpcServerCtx*> servers;
	std::unordered_set<RpcClientCtx*> clients;

	// Shared ownership so callbacks see stable pointers even if Lua GC
	// hasn't run yet and the table is still reachable through a handler
	// lambda capture.
	std::unordered_map<RpcServerCtx*, std::shared_ptr<RpcServerCtx>> server_shared;
	std::unordered_map<RpcClientCtx*, std::shared_ptr<RpcClientCtx>> client_shared;
};

RpcBindState* GetRpcState(lua_State* L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "__RpcBindState");
	auto* state = static_cast<RpcBindState*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return state;
}

RpcBindState* CheckRpcState(lua_State* L) {
	auto* state = GetRpcState(L);
	if (!state) luaL_error(L, "rpc: not initialized");
	return state;
}

// ── Helpers ─────────────────────────────────────────────────────────────

int PushRpcError(lua_State* L, const char* msg) {
	lua_pushnil(L);
	lua_pushstring(L, msg);
	return 2;
}

// ═══════════════════════════════════════════════════════════════════════
// Server methods (called as server:method())
// ═══════════════════════════════════════════════════════════════════════

int l_server_register_service(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "server: closed");

	const char* service_name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TFUNCTION);

	// Release previous callback if re-registering.
	auto it = ctx->service_callbacks.find(service_name);
	if (it != ctx->service_callbacks.end() && it->second != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, it->second);
	}

	lua_pushvalue(L, 3);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ctx->service_callbacks[service_name] = cb_ref;

	auto* rpc_state = GetRpcState(L);
	std::string svc(service_name);
	std::weak_ptr<RpcServerCtx> weak_ctx =
		rpc_state ? rpc_state->server_shared[ctx] : std::weak_ptr<RpcServerCtx>();

	ctx->server->RegisterService(svc,
		[weak_ctx, svc](const std::string& method,
						const std::string& body) -> std::string {
			auto captured = weak_ctx.lock();
			if (!captured || !captured->alive.load(std::memory_order_acquire))
				return R"({"error":"server stopped"})";

			auto req = std::make_unique<PendingRpcCall>();
			req->service = svc;
			req->method = method;
			req->body = body;
			auto future = req->promise.get_future();

			{
				std::lock_guard<std::mutex> lock(captured->queue_mutex);
				captured->pending.push_back(std::move(req));
			}

			auto status = future.wait_for(std::chrono::seconds(5));
			if (status == std::future_status::timeout)
				return R"({"error":"rpc handler timed out"})";
			return future.get();
		});

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RPC service registered: [{}]", service_name);
	lua_pushboolean(L, 1);
	return 1;
}

int l_server_unregister_service(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "server: closed");

	const char* service_name = luaL_checkstring(L, 2);

	auto it = ctx->service_callbacks.find(service_name);
	if (it != ctx->service_callbacks.end()) {
		if (it->second != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
		ctx->service_callbacks.erase(it);
	}

	if (ctx->server) ctx->server->UnregisterService(service_name);

	lua_pushboolean(L, 1);
	return 1;
}

int l_server_stop(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;
	ctx->alive.store(false, std::memory_order_release);

	auto* state = GetRpcState(L);
	if (state) {
		state->servers.erase(ctx);
		// Release shared ownership — weak_ptrs in handler lambdas will
		// fail to lock, causing new invocations to bail out early.
		state->server_shared.erase(ctx);
	}

	// Release all Lua callback references.
	for (auto& [name, ref] : ctx->service_callbacks) {
		if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
	ctx->service_callbacks.clear();

	// Loop-drain the pending queue so every handler that passed the
	// alive check before we set it to false is unblocked.  We retry up
	// to 3 times because a handler may be waiting on queue_mutex
	// during the previous drain pass.  Once the queue is empty all
	// in-flight handlers have returned — safe to destroy the server.
	for (int pass = 0; pass < 3; ++pass) {
		std::vector<std::unique_ptr<PendingRpcCall>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->queue_mutex);
			batch.swap(ctx->pending);
		}
		if (batch.empty()) break;
		for (auto& req : batch) {
			try { req->promise.set_value(R"({"error":"server stopped"})"); } catch (...) {}
		}
	}

	ctx->server.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	lua_pushboolean(L, 1);
	return 1;
}

int l_server_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;
	ctx->alive.store(false, std::memory_order_release);

	auto* state = GetRpcState(L);
	if (state) {
		state->servers.erase(ctx);
		state->server_shared.erase(ctx);
	}

	for (auto& [name, ref] : ctx->service_callbacks) {
		if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
	ctx->service_callbacks.clear();

	for (int pass = 0; pass < 3; ++pass) {
		std::vector<std::unique_ptr<PendingRpcCall>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->queue_mutex);
			batch.swap(ctx->pending);
		}
		if (batch.empty()) break;
		for (auto& req : batch) {
			try { req->promise.set_value(R"({"error":"gc"})"); } catch (...) {}
		}
	}

	ctx->server.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// Client methods (called as client:method())
// ═══════════════════════════════════════════════════════════════════════

int l_client_call(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "client: closed");

	const char* service = luaL_checkstring(L, 2);
	const char* method = luaL_checkstring(L, 3);
	const char* args = luaL_optstring(L, 4, "{}");
	int timeout_ms = static_cast<int>(luaL_optinteger(L, 5, 5000));

	auto resp = ctx->client->CallSync(service, method, args, timeout_ms);

	if (resp.success) {
		lua_pushstring(L, resp.body.c_str());
		lua_pushnil(L);
	} else {
		lua_pushnil(L);
		lua_pushstring(L, resp.error_message.c_str());
	}
	return 2;
}

int l_client_stop(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;

	auto* state = GetRpcState(L);
	if (state) state->clients.erase(ctx);

	ctx->client.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	if (state) state->client_shared.erase(ctx);

	lua_pushboolean(L, 1);
	return 1;
}

int l_client_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;

	auto* state = GetRpcState(L);
	if (state) state->clients.erase(ctx);

	ctx->client.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	if (state) state->client_shared.erase(ctx);
	return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// Module-level functions (rpc.new_server / rpc.new_client)
// ═══════════════════════════════════════════════════════════════════════

// rpc.new_server() → server_table
int l_rpc_new_server(lua_State* L) {
	auto* state = CheckRpcState(L);

	auto sp = std::make_shared<RpcServerCtx>();
	sp->L = L;
	sp->server = std::make_unique<rpc::RpcServer>();
	state->server_shared[sp.get()] = sp;

	auto* ctx = sp.get();

	PushInstanceTable(L, ctx, kServerMetaName);  // t

	lua_pushvalue(L, -1);
	ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);  // t

	state->servers.insert(ctx);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RPC server instance created");
	return 1;
}

// rpc.new_client() → client_table
int l_rpc_new_client(lua_State* L) {
	auto* state = CheckRpcState(L);

	auto sp = std::make_shared<RpcClientCtx>();
	sp->L = L;
	sp->client = std::make_unique<rpc::RpcClient>();
	state->client_shared[sp.get()] = sp;

	auto* ctx = sp.get();

	PushInstanceTable(L, ctx, kClientMetaName);  // t

	lua_pushvalue(L, -1);
	ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);  // t

	state->clients.insert(ctx);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RPC client instance created");
	return 1;
}

// ── Metatable registrations ─────────────────────────────────────────────

const luaL_Reg kServerMethods[] = {
	{"register_service",   l_server_register_service},
	{"unregister_service", l_server_unregister_service},
	{"stop",               l_server_stop},
	{nullptr, nullptr},
};

const luaL_Reg kClientMethods[] = {
	{"call", l_client_call},
	{"stop", l_client_stop},
	{nullptr, nullptr},
};

const luaL_Reg kRpcFuncs[] = {
	{"new_server", l_rpc_new_server},
	{"new_client", l_rpc_new_client},
	{nullptr, nullptr},
};

}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// Public API
// ═════════════════════════════════════════════════════════════════════════

void ExportRpc(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	// Per-VM state
	auto* state = new RpcBindState();
	lua_pushlightuserdata(L, state);
	lua_setfield(L, LUA_REGISTRYINDEX, "__RpcBindState");

	// Metatables
	RegisterInstanceMeta(L, kServerMetaName, kServerMethods, l_server_gc);
	RegisterInstanceMeta(L, kClientMetaName, kClientMethods, l_client_gc);

	// Module table
	vm.RegisterModule("rpc", kRpcFuncs);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RPC API exported to Lua (rpc.new_server / rpc.new_client)");
}

void UpdateRpcBindings(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	auto* state = GetRpcState(L);
	if (!state) return;

	for (auto* ctx : state->servers) {
		if (ctx->disposed) continue;

		std::vector<std::unique_ptr<PendingRpcCall>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->queue_mutex);
			batch.swap(ctx->pending);
		}

		for (auto& req : batch) {
			std::string result = R"({"error":"service not found"})";

			auto it = ctx->service_callbacks.find(req->service);
			if (it != ctx->service_callbacks.end() && it->second != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);             // cb
				lua_pushlstring(L, req->service.data(), req->service.size());  // cb, svc
				lua_pushlstring(L, req->method.data(), req->method.size());    // cb, svc, method
				lua_pushlstring(L, req->body.data(), req->body.size());        // cb, svc, method, body

				if (lua_pcall(L, 3, 1, 0) == LUA_OK) {
					if (lua_isstring(L, -1))
						result = lua_tostring(L, -1);
					else
						result = "{}";
				} else {
					const char* err = lua_tostring(L, -1);
					result = std::string(R"({"error":")") + (err ? err : "lua error") + R"("})";
					auto* logger = GetLogger();
					ENGINE_LOG_ERROR(logger,
						"RPC handler error in service [{}]: {}",
						req->service, err ? err : "unknown");
				}
				lua_pop(L, 1);
			}

			try { req->promise.set_value(result); } catch (...) {}
		}
	}
}

void ShutdownRpcBindings(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	auto* state = GetRpcState(L);
	if (!state) return;

	auto* logger = GetLogger();

	// Move to locals — stop() callbacks may mutate the sets.
	auto servers = std::move(state->servers);
	auto clients = std::move(state->clients);

	for (auto* ctx : servers) {
		if (ctx->disposed) continue;
		ctx->disposed = true;
		ctx->alive.store(false, std::memory_order_release);

		// Release shared ownership so handler weak_ptrs fail to lock.
		state->server_shared.erase(ctx);

		for (auto& [name, ref] : ctx->service_callbacks) {
			if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
		}
		ctx->service_callbacks.clear();

		// Loop-drain before destroying the server — same pattern as
		// l_server_stop: unblocks any in-flight handlers so they return
		// before the std::function objects inside RpcServer are freed.
		for (int pass = 0; pass < 3; ++pass) {
			std::vector<std::unique_ptr<PendingRpcCall>> batch;
			{
				std::lock_guard<std::mutex> lock(ctx->queue_mutex);
				batch.swap(ctx->pending);
			}
			if (batch.empty()) break;
			for (auto& req : batch) {
				try { req->promise.set_value(R"({"error":"shutdown"})"); } catch (...) {}
			}
		}

		ctx->server.reset();

		if (ctx->instance_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
			ctx->instance_ref = LUA_NOREF;
		}
	}

	for (auto* ctx : clients) {
		if (ctx->disposed) continue;
		ctx->disposed = true;
		ctx->client.reset();

		if (ctx->instance_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
			ctx->instance_ref = LUA_NOREF;
		}
	}

	state->server_shared.clear();
	state->client_shared.clear();

	if (!servers.empty() || !clients.empty()) {
		ENGINE_LOG_INFO(logger,
			"RpcBind: shut down [{}] server(s), [{}] client(s)",
			servers.size(), clients.size());
	} else {
		ENGINE_LOG_DEBUG(logger, "RpcBind: no active instances to shut down");
	}

	delete state;
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "__RpcBindState");
}

rpc::RpcServer* RpcBind_GetServer(lua_State* L, int idx) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, idx);
	return ctx ? ctx->server.get() : nullptr;
}

rpc::RpcClient* RpcBind_GetClient(lua_State* L, int idx) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, idx);
	return ctx ? ctx->client.get() : nullptr;
}

}  // namespace script
}  // namespace engine
