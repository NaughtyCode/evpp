#include "runtime/script/net_udp_server_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <atomic>
#include <memory>
#include <string>
#include <unordered_set>

#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/udp/udp_message.h>
#include <runtime/evpp/udp/udp_server.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/script/bind_util.h"
#include "runtime/script/net_lifetime.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// UDP Server bindings (light userdata + Lua class)

struct UdpServerCtx {
	std::unique_ptr<evpp::udp::Server> server;
	lua_State* L = nullptr;
	int instance_ref = LUA_NOREF;  // Lua instance table ref
	// Atomic: written from main thread (listen/set_on_message/stop),
	// read from RecvThread inside MessageHandler.
	std::atomic<int> on_message_ref{LUA_NOREF};
	bool disposed = false;
};

const char* kUdpServerMetaName = "net.udp_server.instance";

// Lightweight set of active UdpServerCtx pointers, used ONLY by
// ShutdownUdpServerBindings to find and stop all servers during engine shutdown.
// Normal operations (stop, pause, continue, is_running, set_on_message)
// never touch this set.
std::unordered_set<UdpServerCtx*> g_udp_server_ctxs;

/* Lifetime guard: prevents RunInLoop callbacks from accessing a freed
 * lua_State during shutdown. See net_lifetime.h for the pattern. */
static NetAliveGuard g_udp_alive;

// ── Bind the MessageHandler ─────────────────────────────────────────
// ctx is guaranteed to be alive while the handler runs because
// Stop(true) waits for all recv threads to exit before the caller
// can release instance_ref or delete ctx.
void BindMessageHandler(UdpServerCtx* ctx) {
	auto* main_loop = Engine::Instance().GetEventLoop();

	ctx->server->SetMessageHandler([ctx, main_loop](evpp::EventLoop*, evpp::udp::MessagePtr& msg) {
		int msg_ref = ctx->on_message_ref;
		if (msg_ref == LUA_NOREF) return;
		if (!main_loop) return;

		// Snapshot message data  - the Message buffer may be reused by the
		// recv thread on the next iteration.
		std::string data(msg->data(), msg->size());
		std::string remote_ip = msg->remote_ip();
		lua_State* L_ptr = ctx->L;

		main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip]() {
			if (!g_udp_alive.TryAcquire()) return;
			if (msg_ref == LUA_NOREF) {
				g_udp_alive.Release();
				return;
			}
			lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);
			if (lua_isnil(L_ptr, -1)) {  // ref was freed
				lua_pop(L_ptr, 1);
				g_udp_alive.Release();
				return;
			}
			lua_pushlstring(L_ptr, data.data(), data.size());
			lua_pushlstring(L_ptr, remote_ip.data(), remote_ip.size());
			int msgh = PushLuaErrorHandlerForCall(L_ptr, 2);
			if (lua_pcall(L_ptr, 2, 0, msgh) != LUA_OK) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(
					logger, "[net.udp_server] on_message error: {}", lua_tostring(L_ptr, -1));
				lua_pop(L_ptr, 1);
			}
			g_udp_alive.Release();
		});
	});
}

// ── Internal cleanup ─────────────────────────────────────────────────

void ReleaseUdpServer(lua_State* L, UdpServerCtx* ctx) {
	ctx->disposed = true;

	// Remove from shutdown tracking BEFORE Stop()  - Stop(true) waits for
	// recv threads.  A Lua callback queued during the wait could
	// theoretically call server:stop() re-entrantly and hit the set.
	g_udp_server_ctxs.erase(ctx);

	// Atomically clear on_message_ref so no new message lambda captures
	// the old ref after we begin teardown.
	int old_msg_ref = ctx->on_message_ref.exchange(LUA_NOREF);

	ctx->server->Stop(true);  // wait for recv threads to exit

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	int old_inst_ref = ctx->instance_ref;
	ctx->instance_ref = LUA_NOREF;

	auto* loop = Engine::Instance().GetEventLoop();
	if (loop && loop->IsRunning()) {
		// Defer unref + delete so pending RunInLoop message callbacks
		// (queued before Stop returned) execute before we free the refs.
		loop->RunInLoop([L, old_msg_ref, old_inst_ref, ctx] {
			if (!g_udp_alive.TryAcquire()) return;
			if (old_msg_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, old_msg_ref);
			}
			if (old_inst_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, old_inst_ref);
			}
			CLOUDENGINE_MEM_DELETE(ctx);
			g_udp_alive.Release();
		});
	} else {
		if (old_msg_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, old_msg_ref);
		}
		if (old_inst_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, old_inst_ref);
		}
		CLOUDENGINE_MEM_DELETE(ctx);
	}
}

// ── net.udp_server.listen(port_or_ports, on_message)  - server_instance ─
int l_udp_server_listen(lua_State* L) {
	// Validate arg type before allocation  - luaL_checkstring errors via
	// longjmp, which would leak ctx if we had already allocated it.
	int arg1_type = lua_type(L, 1);
	if (arg1_type != LUA_TNUMBER && arg1_type != LUA_TSTRING) {
		return luaL_error(L, "expected number or string for port");
	}

	auto* ctx = CLOUDENGINE_MEM_NEW(UdpServerCtx);
	ctx->L = L;

	if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	ctx->server = std::make_unique<evpp::udp::Server>();

	bool ok = false;
	// Number  - single port; string  - pass through (handles "5353" and "53,5353")
	if (arg1_type == LUA_TNUMBER) {
		lua_Integer port64 = luaL_checkinteger(L, 1);
		if (port64 <= 0 || port64 > 65535) {
			CLOUDENGINE_MEM_DELETE(ctx);
			return luaL_error(L, "port out of range");
		}
		int port = static_cast<int>(port64);
		ok = ctx->server->Init(port);
	} else {
		const char* ports_str = luaL_checkstring(L, 1);
		ok = ctx->server->Init(ports_str);
	}

	if (!ok) {
		if (ctx->on_message_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
		}
		CLOUDENGINE_MEM_DELETE(ctx);
		lua_pushnil(L);
		lua_pushstring(L, "udp_server init failed");
		return 2;
	}

	PushInstanceTable(L, ctx, kUdpServerMetaName);

	lua_pushvalue(L, -1);
	ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	BindMessageHandler(ctx);

	if (!ctx->server->Start()) {
		if (ctx->on_message_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
		}
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
		lua_pushnil(L);
		lua_setfield(L, -2, "_ctx");  // null _ctx before delete
		CLOUDENGINE_MEM_DELETE(ctx);
		lua_pop(L, 1);	// pop instance table
		lua_pushnil(L);
		lua_pushstring(L, "udp_server start failed");
		return 2;
	}

	// Track for shutdown (after everything succeeds)
	g_udp_server_ctxs.insert(ctx);

	auto* logger = GetLogger();
	std::string display = (lua_type(L, 1) == LUA_TNUMBER) ? std::to_string(lua_tointeger(L, 1))
														  : std::string(lua_tostring(L, 1));
	ENGINE_LOG_INFO(logger, "[net.udp_server] listening on [{}]", display);

	return 1;  // return the server instance table
}

// ── server:stop()  - bool ───────────────────────────────────────────
int l_udp_server_stop(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.udp_server] stopping server");

	ReleaseUdpServer(L, ctx);

	lua_pushboolean(L, 1);
	return 1;
}

// ── server:pause() ─────────────────────────────────────────────────
int l_udp_server_pause(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx) return luaL_error(L, "udp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "udp_server: closed");
	ctx->server->Pause();
	return 0;
}

// ── server:continue() ──────────────────────────────────────────────
int l_udp_server_continue(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx) return luaL_error(L, "udp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "udp_server: closed");
	ctx->server->Continue();
	return 0;
}

// ── server:is_running()  - bool ─────────────────────────────────────
int l_udp_server_is_running(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, ctx->server->IsRunning() ? 1 : 0);
	return 1;
}

// ── server:set_on_message(callback) ────────────────────────────────
int l_udp_server_set_on_message(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx) return luaL_error(L, "udp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "udp_server: closed");

	// Atomically swap old ref for LUA_NOREF so new message lambdas
	// don't capture it; defer unref so pending RunInLoop tasks that
	// already captured the old ref can still use it.
	int old_ref = ctx->on_message_ref.exchange(LUA_NOREF);

	// Store new callback (or leave cleared if absent/nil)
	if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	// Re-bind the MessageHandler so the new ref is captured
	BindMessageHandler(ctx);

	if (old_ref != LUA_NOREF) {
		auto* loop = Engine::Instance().GetEventLoop();
		if (loop) {
			lua_State* L_ptr = L;
			loop->RunInLoop([L_ptr, old_ref] {
				if (!g_udp_alive.TryAcquire()) return;
				luaL_unref(L_ptr, LUA_REGISTRYINDEX, old_ref);
				g_udp_alive.Release();
			});
		} else {
			luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
		}
	}

	return 0;
}

// ── __gc metamethod ────────────────────────────────────────────────
int l_udp_server_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpServerCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ReleaseUdpServer(L, ctx);

	return 0;
}

// ── Instance method table ──────────────────────────────────────────
const luaL_Reg kUdpServerMethods[] = {
	{"stop", l_udp_server_stop},
	{"pause", l_udp_server_pause},
	{"continue", l_udp_server_continue},
	{"is_running", l_udp_server_is_running},
	{"set_on_message", l_udp_server_set_on_message},
	{nullptr, nullptr},
};

// ── net.udp_server static functions ────────────────────────────────
const luaL_Reg kUdpServerFunctions[] = {
	{"listen", l_udp_server_listen},
	{nullptr, nullptr},
};

}  // namespace

// Public API

void RegisterUdpServerMetaTable(lua_State* L) {
	if (!L) return;

	RegisterInstanceMeta(L, kUdpServerMetaName, kUdpServerMethods, l_udp_server_gc);
}

void PushUdpServerLibrary(lua_State* L) {
	if (!L) return;

	// net.udp_server table (static functions only: listen)
	PushLibrary(L, kUdpServerFunctions);
}

void ShutdownUdpServerBindings() {
	auto* logger = GetLogger();

	/* Step 1: Atomically mark the guard as dead. New RunInLoop callbacks
	 * (message dispatch, cleanup) will see TryAcquire return false and
	 * bail out immediately. */
	g_udp_alive.Shutdown();

	/* Step 2: Wait for all in-flight callbacks to finish their Lua
	 * operations. After this returns, no callback is touching lua_State. */
	g_udp_alive.WaitDrain();

	/* Step 3: Stop all servers. Recv threads join; any RunInLoop callbacks
	 * queued between Shutdown and Stop will bail on TryAcquire. */
	auto ctxs = std::move(g_udp_server_ctxs);
	for (auto* ctx : ctxs) {
		if (ctx->disposed) continue;
		ctx->disposed = true;
		ctx->server->Stop(true);

		/* Step 4: Direct cleanup  - no RunInLoop deferral needed because
		 * WaitDrain guarantees no callback is touching Lua state, and
		 * TryAcquire=false guarantees no future callback will try. */
		int old_msg_ref = ctx->on_message_ref.exchange(LUA_NOREF);
		int old_inst_ref = ctx->instance_ref;
		ctx->instance_ref = LUA_NOREF;
		lua_State* L_ptr = ctx->L;

		if (old_msg_ref != LUA_NOREF && L_ptr) {
			luaL_unref(L_ptr, LUA_REGISTRYINDEX, old_msg_ref);
		}
		if (old_inst_ref != LUA_NOREF && L_ptr) {
			luaL_unref(L_ptr, LUA_REGISTRYINDEX, old_inst_ref);
		}
		CLOUDENGINE_MEM_DELETE(ctx);
	}

	if (!ctxs.empty()) {
		ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] UDP server(s)", ctxs.size());
	} else {
		ENGINE_LOG_DEBUG(logger, "ScriptBind: no active UDP server bindings to shut down");
	}
}

}  // namespace script
}  // namespace engine
