#include "runtime/script/rpc_bind.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

	// service_name �?Lua callback registry ref
	std::unordered_map<std::string, int> service_callbacks;

	// Deferred execution queue �?requests from transport thread wait here
	// until drained by UpdateRpcBindings() on the main thread.
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
	int send_cb_ref = LUA_NOREF;  // Lua registry ref for the send callback
	bool disposed = false;

	// Deferred response queue �?call_async callbacks push here from any
	// thread; UpdateRpcBindings drains on the Lua main thread.
	std::mutex response_mutex;
	std::vector<std::pair<int, rpc::RpcResponse>> deferred_responses;
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

// Drain the pending queue for a server, fulfilling all promises with the
// given error message.  Used by Stop/GC/Shutdown to unblock in-flight
// handlers before the server is destroyed.
// Retries up to 3 passes to catch handlers that were blocked on queue_mutex
// during a prior pass.  Once the queue is empty, all in-flight handlers
// have returned �?safe to destroy the server.
static void DrainPendingQueue(RpcServerCtx* ctx, const char* error_msg) {
	for (int pass = 0; pass < 3; ++pass) {
		std::vector<std::unique_ptr<PendingRpcCall>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->queue_mutex);
			batch.swap(ctx->pending);
		}
		if (batch.empty()) break;
		for (auto& req : batch) {
			try { req->promise.set_value(error_msg); } catch (...) {}
		}
		// Yield between passes so in-flight handlers blocked on
		// queue_mutex have a chance to push their requests.
		if (pass < 2) std::this_thread::yield();
	}
}

// Drain the client deferred response queue, releasing all callback refs.
// Called during stop/gc/shutdown �?no Lua callbacks are invoked since the
// client is being torn down.
static void DrainResponseQueue(RpcClientCtx* ctx, lua_State* L) {
	std::vector<std::pair<int, rpc::RpcResponse>> batch;
	{
		std::lock_guard<std::mutex> lock(ctx->response_mutex);
		batch.swap(ctx->deferred_responses);
	}
	for (auto& [cb_ref, resp] : batch) {
		if (cb_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
	}
}

// ══════════════════════════════════════════════════════════════════════�?
// Server methods (called as server:method())
// ══════════════════════════════════════════════════════════════════════�?

int l_server_register_service(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcServerCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "server: closed");

	const char* service_name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TFUNCTION);

	// Release previous callback if re-registering (before acquiring new ref).
	auto it = ctx->service_callbacks.find(service_name);
	if (it != ctx->service_callbacks.end() && it->second != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, it->second);
	}

	lua_pushvalue(L, 3);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ctx->service_callbacks[service_name] = cb_ref;

	// Build weak_ptr before validating �?GetRpcState should never be null
	// since new_server requires it, but guard anyway.
	auto* rpc_state = GetRpcState(L);
	std::weak_ptr<RpcServerCtx> weak_ctx;
	if (rpc_state) {
		auto sit = rpc_state->server_shared.find(ctx);
		if (sit != rpc_state->server_shared.end()) {
			weak_ctx = sit->second;
		}
	}

	std::string svc(service_name);
	ctx->server->RegisterService(svc,
		[weak_ctx = std::move(weak_ctx), svc](const std::string& method,
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
	}

	// Release all Lua callback references.
	for (auto& [name, ref] : ctx->service_callbacks) {
		if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
	ctx->service_callbacks.clear();

	DrainPendingQueue(ctx, R"({"error":"server stopped"})");
	ctx->server.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	// Release shared ownership LAST �?after all ctx accesses.
	// Weak_ptrs in handler lambdas will then fail to lock.
	if (state) {
		state->server_shared.erase(ctx);
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
	}

	for (auto& [name, ref] : ctx->service_callbacks) {
		if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
	ctx->service_callbacks.clear();

	DrainPendingQueue(ctx, R"({"error":"gc"})");
	ctx->server.reset();

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	// Release shared ownership LAST �?after all ctx accesses.
	if (state) {
		state->server_shared.erase(ctx);
	}

	return 0;
}

// ══════════════════════════════════════════════════════════════════════�?
// Client methods (called as client:method())
// ══════════════════════════════════════════════════════════════════════�?

int l_client_call(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "client: closed");

	if (!ctx->client->HasTransport()) {
		return PushRpcError(L, "client has no transport �?call client:set_send_callback first");
	}

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

// client:call_async(service, method, args, callback)
// Thread-safe: the callback lambda pushes the response into a deferred
// queue; UpdateRpcBindings drains it on the Lua main thread.
int l_client_call_async(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "client: closed");

	if (!ctx->client->HasTransport()) {
		return PushRpcError(L, "client has no transport �?call client:set_send_callback first");
	}

	const char* service = luaL_checkstring(L, 2);
	const char* method = luaL_checkstring(L, 3);
	const char* args = luaL_optstring(L, 4, "{}");
	luaL_checktype(L, 5, LUA_TFUNCTION);

	// Store callback in registry for main-thread invocation.
	lua_pushvalue(L, 5);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	// Build a weak_ptr so the transport-thread lambda can safely
	// detect client destruction without touching the Lua VM.
	auto* rpc_state = GetRpcState(L);
	std::weak_ptr<RpcClientCtx> weak_ctx;
	if (rpc_state) {
		auto it = rpc_state->client_shared.find(ctx);
		if (it != rpc_state->client_shared.end()) {
			weak_ctx = it->second;
		}
	}

	ctx->client->CallAsync(service, method, args,
		[weak_ctx = std::move(weak_ctx), cb_ref](const rpc::RpcResponse& resp) {
			auto captured = weak_ctx.lock();
			if (captured) {
				std::lock_guard<std::mutex> lock(captured->response_mutex);
				captured->deferred_responses.emplace_back(cb_ref, resp);
			}
			// If captured is null the client was destroyed; cb_ref is
			// released by the destruction path (stop/gc/shutdown).
		});

	lua_pushboolean(L, 1);
	return 1;
}

int l_client_set_send_callback(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return PushRpcError(L, "client: closed");

	luaL_checktype(L, 2, LUA_TFUNCTION);

	// Release previous send callback if re-registering.
	if (ctx->send_cb_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->send_cb_ref);
		ctx->send_cb_ref = LUA_NOREF;
	}

	lua_pushvalue(L, 2);
	int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ctx->send_cb_ref = cb_ref;

	// Weak reference to L: the send callback lambda outlives this
	// function call (it's stored inside RpcClient).  L is guaranteed
	// valid until ShutdownRpcBindings destroys the client.  If the
	// client is destroyed via stop/gc, the lambda is destroyed
	// together with RpcClient �?no dangling L.
	lua_State* captured_L = L;
	ctx->client->SetSendCallback([captured_L, cb_ref](rpc::RpcRequest req) {
		lua_rawgeti(captured_L, LUA_REGISTRYINDEX, cb_ref);                      // cb
		lua_pushinteger(captured_L, static_cast<lua_Integer>(req.header.msgid));  // cb, msgid
		lua_pushlstring(captured_L, req.header.service.data(), req.header.service.size());  // cb, msgid, svc
		lua_pushlstring(captured_L, req.header.method.data(), req.header.method.size());    // cb, msgid, svc, mtd
		lua_pushlstring(captured_L, req.body.data(), req.body.size());                      // cb, msgid, svc, mtd, body

		if (lua_pcall(captured_L, 4, 0, 0) != LUA_OK) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger, "RpcClient: send callback error: {}",
							 lua_tostring(captured_L, -1));
			lua_pop(captured_L, 1);
		}
	});

	lua_pushboolean(L, 1);
	return 1;
}

int l_client_stop(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;

	auto* state = GetRpcState(L);
	if (state) {
		state->clients.erase(ctx);
	}

	if (ctx->send_cb_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->send_cb_ref);
		ctx->send_cb_ref = LUA_NOREF;
	}

	// Destroy the client first �?RpcClient::~RpcClient fulfills
	// pending callbacks, which push into deferred_responses.
	ctx->client.reset();

	// Drain deferred responses (release cb_refs without invoking
	// Lua callbacks since the client is being torn down).
	DrainResponseQueue(ctx, L);

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	// Release shared ownership LAST �?after all ctx accesses.
	// This may destroy ctx (if no transport callback holds a ref).
	if (state) {
		state->client_shared.erase(ctx);
	}

	lua_pushboolean(L, 1);
	return 1;
}

int l_client_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<RpcClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;

	auto* state = GetRpcState(L);
	if (state) {
		state->clients.erase(ctx);
	}

	if (ctx->send_cb_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->send_cb_ref);
		ctx->send_cb_ref = LUA_NOREF;
	}

	ctx->client.reset();
	DrainResponseQueue(ctx, L);

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	// Release shared ownership LAST �?after all ctx accesses.
	if (state) {
		state->client_shared.erase(ctx);
	}

	return 0;
}

// ══════════════════════════════════════════════════════════════════════�?
// Module-level functions (rpc.new_server / rpc.new_client)
// ══════════════════════════════════════════════════════════════════════�?

// rpc.new_server() �?server_table
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

// rpc.new_client() �?client_table
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
	{"call",                l_client_call},
	{"call_async",          l_client_call_async},
	{"set_send_callback",   l_client_set_send_callback},
	{"stop",                l_client_stop},
	{nullptr, nullptr},
};

const luaL_Reg kRpcFuncs[] = {
	{"new_server", l_rpc_new_server},
	{"new_client", l_rpc_new_client},
	{nullptr, nullptr},
};

}  // namespace

// ════════════════════════════════════════════════════════════════════════�?
// Public API
// ════════════════════════════════════════════════════════════════════════�?

void ExportRpc(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	// Per-VM state
	auto* state = MEM_NEW(RpcBindState);
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

	// ── Process client timeouts FIRST (cheap map scan, guarantees
	//     they run every frame regardless of server load) ─────────────

	for (auto* ctx : state->clients) {
		if (ctx->disposed) continue;
		ctx->client->ProcessTimeouts();
	}

	// ── Drain client deferred response queues (call_async responses) ──

	for (auto* ctx : state->clients) {
		if (ctx->disposed) continue;

		std::vector<std::pair<int, rpc::RpcResponse>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->response_mutex);
			batch.swap(ctx->deferred_responses);
		}

		for (auto& [cb_ref, resp] : batch) {
			if (cb_ref == LUA_NOREF) continue;

			lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);  // cb
			if (resp.success) {
				lua_pushlstring(L, resp.body.data(), resp.body.size());  // cb, body
				lua_pushnil(L);                                          // cb, body, nil
			} else {
				lua_pushnil(L);                                                  // cb, nil
				lua_pushlstring(L, resp.error_message.data(),
								resp.error_message.size());                       // cb, nil, err
			}

			if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(logger, "RpcClient: call_async callback error: {}",
								 lua_tostring(L, -1));
				lua_pop(L, 1);
			}

			luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
		}
	}

	// ── Process server pending queues (with 5ms time budget) ──────────

	auto budget_start = std::chrono::steady_clock::now();
	constexpr auto kMaxBudget = std::chrono::milliseconds(5);

	for (auto* ctx : state->servers) {
		if (ctx->disposed) continue;

		std::vector<std::unique_ptr<PendingRpcCall>> batch;
		{
			std::lock_guard<std::mutex> lock(ctx->queue_mutex);
			batch.swap(ctx->pending);
		}

		for (auto& req : batch) {
			// Check time budget �?stop processing if we've used >5ms.
			// Re-enqueue remaining items so their promises are NOT
			// destroyed (which would throw future_error in the
			// waiting transport thread).
			if (std::chrono::steady_clock::now() - budget_start >= kMaxBudget) {
				std::vector<std::unique_ptr<PendingRpcCall>> leftover(
					std::make_move_iterator(batch.begin() + (&req - batch.data())),
					std::make_move_iterator(batch.end()));
				{
					std::lock_guard<std::mutex> lock(ctx->queue_mutex);
					ctx->pending.insert(ctx->pending.end(),
						std::make_move_iterator(leftover.begin()),
						std::make_move_iterator(leftover.end()));
				}
				return;
			}

			std::string result = R"({"error":"service not found"})";

			auto it = ctx->service_callbacks.find(req->service);
			if (it != ctx->service_callbacks.end() && it->second != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);                  // cb
				lua_pushlstring(L, req->service.data(), req->service.size());   // cb, svc
				lua_pushlstring(L, req->method.data(), req->method.size());     // cb, svc, method
				lua_pushlstring(L, req->body.data(), req->body.size());         // cb, svc, method, body

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

	// Move to locals �?stop() callbacks may mutate the sets.
	auto servers = std::move(state->servers);
	auto clients = std::move(state->clients);

	for (auto* ctx : servers) {
		if (ctx->disposed) continue;
		ctx->disposed = true;
		ctx->alive.store(false, std::memory_order_release);

		for (auto& [name, ref] : ctx->service_callbacks) {
			if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
		}
		ctx->service_callbacks.clear();

		DrainPendingQueue(ctx, R"({"error":"shutdown"})");
		ctx->server.reset();

		if (ctx->instance_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
			ctx->instance_ref = LUA_NOREF;
		}

		// Release shared ownership LAST �?after all ctx accesses.
		state->server_shared.erase(ctx);
	}

	// Client cleanup: RpcClient::~RpcClient fulfills pending promises
	// and invokes call_async callbacks �?pushes to deferred_responses.
	// Drain those to release cb_refs.
	for (auto* ctx : clients) {
		if (ctx->disposed) continue;
		ctx->disposed = true;

		if (ctx->send_cb_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->send_cb_ref);
			ctx->send_cb_ref = LUA_NOREF;
		}

		ctx->client.reset();
		DrainResponseQueue(ctx, L);

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

	MEM_DELETE(state);
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
