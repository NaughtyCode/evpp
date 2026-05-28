#include "runtime/script/net_tcp_client_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <runtime/evpp/buffer.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/tcp_client.h>
#include <runtime/evpp/tcp_conn.h>

#include "runtime/config/limits.h"
#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/script/bind_util.h"
#include "runtime/network/length_prefixed_codec.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// TCP Client bindings (light userdata + Lua class)

struct ClientCtx {
	std::unique_ptr<evpp::TCPClient> client;
	lua_State* L = nullptr;
	int instance_ref = LUA_NOREF;  // ref to Lua class instance table
	engine::LengthPrefixedCodec codec;  /* message framing */
	bool is_connected = false;
	bool disposed = false;
};

const char* kClientMetaName = "net.client.instance";

// Global tracking for active client connections — enables explicit
// shutdown without relying on Lua GC.
std::mutex g_client_ctxs_mutex;
std::unordered_map<evpp::TCPClient*, ClientCtx*> g_client_ctxs;

// Shared ownership — replaces RunInLoop([del_ctx]{ delete del_ctx; })
std::unordered_map<ClientCtx*, std::shared_ptr<ClientCtx>> g_client_shared;

void RegisterClientCtx(ClientCtx* ctx) {
	std::lock_guard<std::mutex> lock(g_client_ctxs_mutex);
	g_client_ctxs[ctx->client.get()] = ctx;
}

void UnregisterClientCtx(ClientCtx* ctx) {
	std::lock_guard<std::mutex> lock(g_client_ctxs_mutex);
	g_client_ctxs.erase(ctx->client.get());
}


// ── l_net_client_connect(addr)  - instance_table ──
int l_net_client_connect(lua_State* L) {
	const char* addr = luaL_checkstring(L, 1);
	if (!*addr) {
		return luaL_error(L, "address must not be empty");
	}

	auto* loop = Engine::Instance().GetEventLoop();
	if (!loop) {
		return luaL_error(L, "EventLoop not available");
	}

	auto sp = std::make_shared<ClientCtx>();
	g_client_shared[sp.get()] = sp;
	auto* ctx = sp.get();
	ctx->L = L;

	// Build Lua class instance table
	PushInstanceTable(L, ctx, kClientMetaName);  // t

	// Ref instance table in registry for callbacks
	lua_pushvalue(L, -1);  // t, t
	ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);	 // t

	// Create TCPClient
	auto name = std::string("lua_client_") + std::to_string(reinterpret_cast<uintptr_t>(ctx));
	ctx->client = std::make_unique<evpp::TCPClient>(loop, addr, name);
	ctx->client->set_auto_reconnect(false);
	RegisterClientCtx(ctx);

	auto* L_ptr = L;
	int inst_ref = ctx->instance_ref;
	ClientCtx* ctx_ptr = ctx;

	ctx->client->SetConnectionCallback([L_ptr, inst_ref, ctx_ptr](const evpp::TCPConnPtr& conn) {
		if (ctx_ptr->disposed) return;

		if (conn->IsConnected()) {
			ctx_ptr->is_connected = true;
			auto* logger = GetLogger();
			ENGINE_LOG_INFO(logger, "[net.client] connected: remote=[{}]", conn->remote_addr());
			CallInstMethod(L_ptr, inst_ref, "on_connect");
		} else {
			ctx_ptr->is_connected = false;
			auto* logger = GetLogger();
			ENGINE_LOG_INFO(logger, "[net.client] disconnected");

			// Set disposed BEFORE dispatching on_close: the Lua handler
			// may call disconnect() re-entrantly which queues delete.
			// If we checked disposed after the callback, ctx_ptr could
			// already be freed.
			bool already_disposed = ctx_ptr->disposed;
			ctx_ptr->disposed = true;
			UnregisterClientCtx(ctx_ptr);

			CallInstMethod(L_ptr, inst_ref, "on_close");

			if (!already_disposed) {
				if (inst_ref != LUA_NOREF) {
					lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, inst_ref);
					lua_pushnil(L_ptr);
					lua_setfield(L_ptr, -2, "_ctx");  // prevent use-after-free
					lua_pop(L_ptr, 1);
					luaL_unref(L_ptr, LUA_REGISTRYINDEX, inst_ref);
					ctx_ptr->instance_ref = LUA_NOREF;
				}
				g_client_shared.erase(ctx_ptr);
			}
		}
	});

	ctx->client->SetMessageCallback(
		[L_ptr, inst_ref, ctx_ptr](const evpp::TCPConnPtr&, evpp::Buffer* buf) {
			if (ctx_ptr->disposed) return;
			auto messages = ctx_ptr->codec.Decode(buf);
			for (const auto& data : messages) {
				CallInstMethodStr(L_ptr, inst_ref, "on_message", data);
			}
		});

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.client] connecting to [{}]", addr);
	ctx->client->Connect();

	return 1;  // return the instance table
}

// ── client:send(data) ────────────────────────────────────────────────────
int l_client_send(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "client: invalid context");
	if (ctx->disposed) return luaL_error(L, "client: closed");

	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);

		if (len > ctx->codec.GetMaxMessageSize()) {
			return luaL_error(L, "message size %zu exceeds limit %u",
					 len, ctx->codec.GetMaxMessageSize());
		}

	auto conn = ctx->client->conn();
	if (!conn || !conn->IsConnected()) {
		return luaL_error(L, "client: not connected");
	}

		/* Encode with length-prefixed framing so the receiver can split messages */
		std::string framed = ctx->codec.Encode(std::string(data, len));
		if (!framed.empty()) {
			conn->Send(framed.data(), framed.size());
		}
	return 0;
}

// ── client:disconnect()  - bool ───────────────────────────────────────────
int l_client_disconnect(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;
	UnregisterClientCtx(ctx);

	// Null out _ctx in the instance table to prevent use-after-free
	// from subsequent method calls after the deferred delete runs.
	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.client] disconnecting");

	// Clear callbacks before Disconnect()  - TCPConn::Close() uses
	// QueueInLoop (always defers), so HandleClose may execute after
	// delete ctx below, and the stored callbacks capture raw ClientCtx*.
	ctx->client->SetConnectionCallback(evpp::ConnectionCallback());
	ctx->client->SetMessageCallback(evpp::MessageCallback());
	ctx->client->Disconnect();

	g_client_shared.erase(ctx);

	lua_pushboolean(L, 1);
	return 1;
}

// ── client:is_connected()  - bool ─────────────────────────────────────────
int l_client_is_connected(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	auto conn = ctx->client->conn();
	lua_pushboolean(L, (conn && conn->IsConnected()) ? 1 : 0);
	return 1;
}

// ── __gc metamethod ──────────────────────────────────────────────────────
int l_client_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;
	ctx->is_connected = false;
	UnregisterClientCtx(ctx);

	// Null out _ctx to prevent use-after-free from subsequent accesses
	// (though the table is being collected, methods may still be callable
	// from finalizer ordering edge cases).
	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	if (ctx->instance_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
	}

	// Clear callbacks to prevent use-after-free (same rationale as
	// l_client_disconnect above).
	ctx->client->SetConnectionCallback(evpp::ConnectionCallback());
	ctx->client->SetMessageCallback(evpp::MessageCallback());
	ctx->client->Disconnect();

	g_client_shared.erase(ctx);

	return 0;
}

// ── client:set_on_connect(callback) ────────────────────────────────────────
int l_client_set_on_connect(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "client: invalid context");
	if (ctx->disposed) return luaL_error(L, "client: closed");
	lua_settop(L, 2);
	if (!lua_isnil(L, 2) && !lua_isfunction(L, 2)) {
		return luaL_error(L, "expected function or nil");
	}
	lua_setfield(L, 1, "on_connect");
	return 0;
}

// ── client:set_on_message(callback) ───────────────────────────────────────
int l_client_set_on_message(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "client: invalid context");
	if (ctx->disposed) return luaL_error(L, "client: closed");
	lua_settop(L, 2);
	if (!lua_isnil(L, 2) && !lua_isfunction(L, 2)) {
		return luaL_error(L, "expected function or nil");
	}
	lua_setfield(L, 1, "on_message");
	return 0;
}

// ── client:set_on_close(callback) ─────────────────────────────────────────
int l_client_set_on_close(lua_State* L) {
	auto* ctx = GetCtxFromTable<ClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "client: invalid context");
	if (ctx->disposed) return luaL_error(L, "client: closed");
	lua_settop(L, 2);
	if (!lua_isnil(L, 2) && !lua_isfunction(L, 2)) {
		return luaL_error(L, "expected function or nil");
	}
	lua_setfield(L, 1, "on_close");
	return 0;
}

// ── Instance method table ─────────────────────────────────────────────────
const luaL_Reg kClientMethods[] = {
	{"send", l_client_send},
	{"disconnect", l_client_disconnect},
	{"is_connected", l_client_is_connected},
	{"set_on_connect", l_client_set_on_connect},
	{"set_on_message", l_client_set_on_message},
	{"set_on_close", l_client_set_on_close},
	{nullptr, nullptr},
};

// ── net.client static functions ───────────────────────────────────────────
const luaL_Reg kClientFunctions[] = {
	{"connect", l_net_client_connect},
	{nullptr, nullptr},
};

}  // namespace

// Public API

void RegisterClientMetaTable(lua_State* L) {
	if (!L) return;

	RegisterInstanceMeta(L, kClientMetaName, kClientMethods, l_client_gc);
}

void PushClientLibrary(lua_State* L) {
	if (!L) return;

	// net.client table (static functions only: connect)
	PushLibrary(L, kClientFunctions);  // client
}

void ShutdownClientBindings() {
	// Step 1: Snapshot all active contexts under lock.
	std::vector<ClientCtx*> ctxs;
	{
		std::lock_guard<std::mutex> lock(g_client_ctxs_mutex);
		ctxs.reserve(g_client_ctxs.size());
		for (auto& [client, ctx] : g_client_ctxs) {
			ctxs.push_back(ctx);
		}
		g_client_ctxs.clear();
	}

	// Step 2: Dispose each client, close connection, unref, clear
	// callbacks, and queue deletion on the event loop.
	for (auto* ctx : ctxs) {
		ctx->disposed = true;

		if (ctx->client) {
			ctx->client->SetConnectionCallback(evpp::ConnectionCallback());
			ctx->client->SetMessageCallback(evpp::MessageCallback());
			ctx->client->Disconnect();
		}

		if (ctx->instance_ref != LUA_NOREF && ctx->L) {
			luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->instance_ref);
			ctx->instance_ref = LUA_NOREF;
		}

		g_client_shared.erase(ctx);
	}
}

}  // namespace script
}  // namespace engine
