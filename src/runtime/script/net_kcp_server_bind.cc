#include "runtime/script/net_kcp_server_bind.h"

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
#include <runtime/evpp/kcp/kcp_message.h>
#include <runtime/evpp/kcp/kcp_server.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/script/net_lifetime.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// ======================================================================
// KCP Server bindings (light userdata + Lua class)
// ======================================================================

struct KcpServerCtx {
	std::unique_ptr<evpp::kcp::Server> server;
	lua_State* L = nullptr;
	int instance_ref = LUA_NOREF;  // Lua instance table ref
	std::atomic<int> on_message_ref{LUA_NOREF};
	bool disposed = false;
};

const char* kKcpServerMetaName = "net.kcp_server.instance";

// Lightweight set of active KcpServerCtx pointers, used ONLY by
// ShutdownKcpServerBindings to find and stop all servers during engine shutdown.
std::unordered_set<KcpServerCtx*> g_kcp_server_ctxs;

/* Lifetime guard: prevents RunInLoop callbacks from accessing a freed
 * lua_State during shutdown. See net_lifetime.h for the pattern. */
static NetAliveGuard g_kcp_alive;

// ── Internal helpers ─────────────────────────────────────────────────

KcpServerCtx* GetKcpServerCtxFromTable(lua_State* L, int idx) {
	lua_getfield(L, idx, "_ctx");
	auto* ctx = static_cast<KcpServerCtx*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return ctx;
}

// ── Bind the MessageHandler ─────────────────────────────────────────
void BindKcpMessageHandler(KcpServerCtx* ctx) {
	auto* main_loop = Engine::Instance().GetEventLoop();

	ctx->server->SetMessageHandler([ctx, main_loop](evpp::EventLoop*, evpp::kcp::MessagePtr& msg) {
		int msg_ref = ctx->on_message_ref;
		if (msg_ref == LUA_NOREF) return;
		if (!main_loop) return;

		// Snapshot message data — the Message buffer may be reused by the
		// recv thread on the next iteration.
		std::string data(msg->data(), msg->size());
		std::string remote_ip = msg->remote_ip();
		uint32_t conv = msg->conv();
		lua_State* L_ptr = ctx->L;

		main_loop->RunInLoop([L_ptr, msg_ref, data, remote_ip, conv]() {
			if (!g_kcp_alive.TryAcquire()) return;
			if (msg_ref == LUA_NOREF) {
				g_kcp_alive.Release();
				return;
			}
			lua_rawgeti(L_ptr, LUA_REGISTRYINDEX, msg_ref);
			if (lua_isnil(L_ptr, -1)) {
				lua_pop(L_ptr, 1);
				g_kcp_alive.Release();
				return;
			}
			lua_pushlstring(L_ptr, data.data(), data.size());
			lua_pushlstring(L_ptr, remote_ip.data(), remote_ip.size());
			lua_pushinteger(L_ptr, conv);
			if (lua_pcall(L_ptr, 3, 0, 0) != LUA_OK) {
				auto* logger = GetLogger();
				ENGINE_LOG_ERROR(
					logger, "[net.kcp_server] on_message error: {}", lua_tostring(L_ptr, -1));
				lua_pop(L_ptr, 1);
			}
			g_kcp_alive.Release();
		});
	});
}

// ── Internal cleanup ─────────────────────────────────────────────────

void ReleaseKcpServer(lua_State* L, KcpServerCtx* ctx) {
	ctx->disposed = true;

	g_kcp_server_ctxs.erase(ctx);

	int old_msg_ref = ctx->on_message_ref.exchange(LUA_NOREF);

	ctx->server->Stop(true);

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	int old_inst_ref = ctx->instance_ref;
	ctx->instance_ref = LUA_NOREF;

	auto* loop = Engine::Instance().GetEventLoop();
	if (loop && loop->IsRunning()) {
		loop->RunInLoop([L, old_msg_ref, old_inst_ref, ctx] {
			if (!g_kcp_alive.TryAcquire()) return;
			if (old_msg_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, old_msg_ref);
			}
			if (old_inst_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, old_inst_ref);
			}
			delete ctx;
			g_kcp_alive.Release();
		});
	} else {
		if (old_msg_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, old_msg_ref);
		}
		if (old_inst_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, old_inst_ref);
		}
		delete ctx;
	}
}

// ── net.kcp_server.listen(port_or_ports, on_message) → server_instance ─
int l_kcp_server_listen(lua_State* L) {
	int arg1_type = lua_type(L, 1);
	if (arg1_type != LUA_TNUMBER && arg1_type != LUA_TSTRING) {
		return luaL_error(L, "expected number or string for port");
	}

	auto* ctx = new KcpServerCtx();
	ctx->L = L;

	if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	ctx->server = std::make_unique<evpp::kcp::Server>();

	bool ok = false;
	if (arg1_type == LUA_TNUMBER) {
		lua_Integer port64 = luaL_checkinteger(L, 1);
		if (port64 <= 0 || port64 > 65535) {
			if (ctx->on_message_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
			}
			delete ctx;
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
		delete ctx;
		lua_pushnil(L);
		lua_pushstring(L, "kcp_server init failed");
		return 2;
	}

	// Build Lua class instance table
	lua_newtable(L);

	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, -2, "_ctx");

	luaL_getmetatable(L, kKcpServerMetaName);
	lua_setmetatable(L, -2);

	lua_pushvalue(L, -1);
	ctx->instance_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	BindKcpMessageHandler(ctx);

	if (!ctx->server->Start()) {
		if (ctx->on_message_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->on_message_ref);
		}
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->instance_ref);
		ctx->instance_ref = LUA_NOREF;
		lua_pushnil(L);
		lua_setfield(L, -2, "_ctx");
		delete ctx;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "kcp_server start failed");
		return 2;
	}

	g_kcp_server_ctxs.insert(ctx);

	auto* logger = GetLogger();
	std::string display = (lua_type(L, 1) == LUA_TNUMBER) ? std::to_string(lua_tointeger(L, 1))
														  : std::string(lua_tostring(L, 1));
	ENGINE_LOG_INFO(logger, "[net.kcp_server] listening on [{}]", display);

	return 1;
}

// ── server:stop() → bool ───────────────────────────────────────────
int l_kcp_server_stop(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.kcp_server] stopping server");

	ReleaseKcpServer(L, ctx);

	lua_pushboolean(L, 1);
	return 1;
}

// ── server:pause() ─────────────────────────────────────────────────
int l_kcp_server_pause(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	ctx->server->Pause();
	return 0;
}

// ── server:continue() ──────────────────────────────────────────────
int l_kcp_server_continue(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	ctx->server->Continue();
	return 0;
}

// ── server:is_running() → bool ─────────────────────────────────────
int l_kcp_server_is_running(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, ctx->server->IsRunning() ? 1 : 0);
	return 1;
}

// ── server:set_on_message(callback) ────────────────────────────────
int l_kcp_server_set_on_message(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");

	int old_ref = ctx->on_message_ref.exchange(LUA_NOREF);

	if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		ctx->on_message_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	BindKcpMessageHandler(ctx);

	if (old_ref != LUA_NOREF) {
		auto* loop = Engine::Instance().GetEventLoop();
		if (loop) {
			lua_State* L_ptr = L;
			loop->RunInLoop([L_ptr, old_ref] {
				if (!g_kcp_alive.TryAcquire()) return;
				luaL_unref(L_ptr, LUA_REGISTRYINDEX, old_ref);
				g_kcp_alive.Release();
			});
		} else {
			luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
		}
	}

	return 0;
}

// ── server:set_kcp_nodelay(nodelay, interval, resend, nc) ──────────
int l_kcp_server_set_kcp_nodelay(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	int nodelay = static_cast<int>(luaL_checkinteger(L, 2));
	int interval = static_cast<int>(luaL_checkinteger(L, 3));
	int resend = static_cast<int>(luaL_checkinteger(L, 4));
	int nc = static_cast<int>(luaL_checkinteger(L, 5));
	ctx->server->SetKcpNodelay(nodelay, interval, resend, nc);
	return 0;
}

// ── server:set_kcp_wnd_size(sndwnd, rcvwnd) ───────────────────────
int l_kcp_server_set_kcp_wnd_size(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	int sndwnd = static_cast<int>(luaL_checkinteger(L, 2));
	int rcvwnd = static_cast<int>(luaL_checkinteger(L, 3));
	ctx->server->SetKcpWndSize(sndwnd, rcvwnd);
	return 0;
}

// ── server:set_kcp_mtu(mtu) ───────────────────────────────────────
int l_kcp_server_set_kcp_mtu(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	int mtu = static_cast<int>(luaL_checkinteger(L, 2));
	ctx->server->SetKcpMtu(mtu);
	return 0;
}

// ── server:set_session_timeout(timeout_ms) ─────────────────────────
int l_kcp_server_set_session_timeout(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx) return luaL_error(L, "kcp_server: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_server: closed");
	lua_Integer t = luaL_checkinteger(L, 2);
	if (t < 0 || t > UINT32_MAX) {
		return luaL_error(L, "timeout_ms out of range");
	}
	ctx->server->SetSessionTimeoutMs(static_cast<uint32_t>(t));
	return 0;
}

// ── __gc metamethod ────────────────────────────────────────────────
int l_kcp_server_gc(lua_State* L) {
	auto* ctx = GetKcpServerCtxFromTable(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ReleaseKcpServer(L, ctx);

	return 0;
}

// ── Instance method table ──────────────────────────────────────────
const luaL_Reg kKcpServerMethods[] = {
	{"stop", l_kcp_server_stop},
	{"pause", l_kcp_server_pause},
	{"continue", l_kcp_server_continue},
	{"is_running", l_kcp_server_is_running},
	{"set_on_message", l_kcp_server_set_on_message},
	{"set_kcp_nodelay", l_kcp_server_set_kcp_nodelay},
	{"set_kcp_wnd_size", l_kcp_server_set_kcp_wnd_size},
	{"set_kcp_mtu", l_kcp_server_set_kcp_mtu},
	{"set_session_timeout", l_kcp_server_set_session_timeout},
	{nullptr, nullptr},
};

// ── net.kcp_server static functions ────────────────────────────────
const luaL_Reg kKcpServerFunctions[] = {
	{"listen", l_kcp_server_listen},
	{nullptr, nullptr},
};

}  // namespace

// ======================================================================
// Public API
// ======================================================================

void RegisterKcpServerMetaTable(lua_State* L) {
	if (!L) return;

	luaL_newmetatable(L, kKcpServerMetaName);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, kKcpServerMethods, 0);
	lua_pushcfunction(L, l_kcp_server_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
}

void PushKcpServerLibrary(lua_State* L) {
	if (!L) return;

	luaL_newlib(L, kKcpServerFunctions);
}

void ShutdownKcpServerBindings() {
	auto* logger = GetLogger();

	/* Step 1: Atomically mark the guard as dead. New RunInLoop callbacks
	 * (message dispatch, cleanup) will see TryAcquire return false and
	 * bail out immediately. */
	g_kcp_alive.Shutdown();

	/* Step 2: Wait for all in-flight callbacks to finish their Lua
	 * operations. After this returns, no callback is touching lua_State. */
	g_kcp_alive.WaitDrain();

	/* Step 3: Stop all servers. Recv threads join; any RunInLoop callbacks
	 * queued between Shutdown and Stop will bail on TryAcquire. */
	auto ctxs = std::move(g_kcp_server_ctxs);
	for (auto* ctx : ctxs) {
		if (ctx->disposed) continue;
		ctx->disposed = true;
		ctx->server->Stop(true);

		/* Step 4: Direct cleanup — no RunInLoop deferral needed because
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
		delete ctx;
	}

	if (!ctxs.empty()) {
		ENGINE_LOG_INFO(logger, "ScriptBind: shut down [{}] KCP server(s)", ctxs.size());
	} else {
		ENGINE_LOG_DEBUG(logger, "ScriptBind: no active KCP server bindings to shut down");
	}
}

}  // namespace script
}  // namespace engine
