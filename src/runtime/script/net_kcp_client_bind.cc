#include "runtime/script/net_kcp_client_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <memory>
#include <string>

#include <runtime/evpp/kcp/sync_kcp_client.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/script/bind_util.h"

extern "C" {
#include "lauxlib.h"
#include "runtime/config/config.h"
#include "runtime/config/limits.h"
}

namespace engine {
namespace script {

namespace {

// KCP Client bindings (light userdata + Lua class)

struct KcpClientCtx {
	std::unique_ptr<evpp::kcp::sync::Client> client;
	bool connected = false;
	bool disposed = false;
	uint32_t conv = 0x11223344;
};

const char* kKcpClientMetaName = "net.kcp_client.instance";

// ── net.kcp_client.new([conv])  - instance_table ────────────────────
// Creates an unconnected instance  - useful when KCP tuning is needed
// before calling instance:connect().
int l_kcp_client_new(lua_State* L) {
	uint32_t conv = 0x11223344;
	if (lua_gettop(L) >= 1) {
		lua_Integer c = luaL_checkinteger(L, 1);
		if (c < 0 || c > UINT32_MAX) {
			return luaL_error(L, "conv out of range");
		}
		conv = static_cast<uint32_t>(c);
	}

	auto* ctx = MEM_NEW(KcpClientCtx);
	ctx->conv = conv;
	ctx->client = std::make_unique<evpp::kcp::sync::Client>();
	ctx->client->SetKcpConv(conv);

	PushInstanceTable(L, ctx, kKcpClientMetaName);

	return 1;
}

// ── net.kcp_client.connect(host, port[, conv])  - instance_table ────
int l_kcp_client_connect_static(lua_State* L) {
	const char* host = luaL_checkstring(L, 1);
	if (!*host) {
		return luaL_error(L, "host must not be empty");
	}
	lua_Integer port64 = luaL_checkinteger(L, 2);
	if (port64 <= 0 || port64 > 65535) {
		return luaL_error(L, "port out of range");
	}
	int port = static_cast<int>(port64);

	uint32_t conv = 0x11223344;
	if (lua_gettop(L) >= 3) {
		lua_Integer c = luaL_checkinteger(L, 3);
		if (c < 0 || c > UINT32_MAX) {
			return luaL_error(L, "conv out of range");
		}
		conv = static_cast<uint32_t>(c);
	}

	auto* ctx = MEM_NEW(KcpClientCtx);
	ctx->conv = conv;

	PushInstanceTable(L, ctx, kKcpClientMetaName);

	ctx->client = std::make_unique<evpp::kcp::sync::Client>();
	if (!ctx->client->Connect(host, port, conv)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(
			logger, "[net.kcp_client] failed to connect to {}:{} conv={}", host, port, conv);
		ctx->disposed = true;
		lua_pushnil(L);
		lua_setfield(L, -2, "_ctx");
		MEM_DELETE(ctx);
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushfstring(L, "kcp connect failed: %s:%d", host, port);
		return 2;
	}

	ctx->connected = true;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.kcp_client] connected to [{}:{}] conv={}", host, port, conv);

	return 1;
}

// ── instance:connect(host, port)  - bool ────────────────────────────
// Connects an instance created via new() (applying any tuning set
// before this call).
int l_kcp_client_connect(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");
	if (ctx->connected) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "already connected");
		return 2;
	}

	const char* host = luaL_checkstring(L, 2);
	lua_Integer port64 = luaL_checkinteger(L, 3);
	if (!*host) {
		return luaL_error(L, "host must not be empty");
	}
	if (port64 <= 0 || port64 > 65535) {
		return luaL_error(L, "port out of range");
	}
	int port = static_cast<int>(port64);

	if (!ctx->client) {
		ctx->client = std::make_unique<evpp::kcp::sync::Client>();
		ctx->client->SetKcpConv(ctx->conv);
	}

	if (!ctx->client->Connect(host, port, ctx->conv)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(
			logger, "[net.kcp_client] failed to connect to {}:{} conv={}", host, port, ctx->conv);
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "kcp connect failed: %s:%d", host, port);
		return 2;
	}

	ctx->connected = true;
	lua_pushboolean(L, 1);
	return 1;
}

// ── instance:send(data)  - bool ─────────────────────────────────────
int l_kcp_client_send(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");

	size_t len = 0;

	const char* data = luaL_checklstring(L, 2, &len);
		{
			uint32_t limit = ConfigManager::Instance().GetServerConfig().resource_limits.max_message_size;
			if (len > limit) {
				return luaL_error(L, "message size %zu exceeds limit %u", len, limit);
			}
		}

	bool ok = ctx->client->Send(data, len);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// ── instance:do_request(data, timeout_ms)  - string ─────────────────
int l_kcp_client_do_request(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");

	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);
	lua_Integer t = luaL_optinteger(L, 3, 3000);
	if (t < 0) {
		return luaL_error(L, "timeout must be >= 0");
	}
	if (t > UINT32_MAX) {
		return luaL_error(L, "timeout too large");
	}
	auto timeout_ms = static_cast<uint32_t>(t);

	std::string resp = ctx->client->DoRequest(std::string(data, len), timeout_ms);
	lua_pushlstring(L, resp.data(), resp.size());
	return 1;
}

// ── instance:close() ───────────────────────────────────────────────
int l_kcp_client_close(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	ctx->client->Close();
	MEM_DELETE(ctx);

	lua_pushboolean(L, 1);
	return 1;
}

// ── instance:is_connected()  - bool ─────────────────────────────────
int l_kcp_client_is_connected(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, ctx->connected ? 1 : 0);
	return 1;
}

// ── instance:set_kcp_nodelay(nodelay, interval, resend, nc) ───────
// Must be called before connect().
int l_kcp_client_set_kcp_nodelay(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");
	int nodelay = static_cast<int>(luaL_checkinteger(L, 2));
	int interval = static_cast<int>(luaL_checkinteger(L, 3));
	int resend = static_cast<int>(luaL_checkinteger(L, 4));
	int nc = static_cast<int>(luaL_checkinteger(L, 5));
	ctx->client->SetKcpNodelay(nodelay, interval, resend, nc);
	return 0;
}

// ── instance:set_kcp_wnd_size(sndwnd, rcvwnd) ─────────────────────
int l_kcp_client_set_kcp_wnd_size(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");
	int sndwnd = static_cast<int>(luaL_checkinteger(L, 2));
	int rcvwnd = static_cast<int>(luaL_checkinteger(L, 3));
	ctx->client->SetKcpWndSize(sndwnd, rcvwnd);
	return 0;
}

// ── instance:set_kcp_mtu(mtu) ─────────────────────────────────────
int l_kcp_client_set_kcp_mtu(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");
	int mtu = static_cast<int>(luaL_checkinteger(L, 2));
	ctx->client->SetKcpMtu(mtu);
	return 0;
}

// ── instance:set_kcp_conv(conv) ────────────────────────────────────
int l_kcp_client_set_kcp_conv(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "kcp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "kcp_client: closed");
	lua_Integer c = luaL_checkinteger(L, 2);
	if (c < 0 || c > UINT32_MAX) {
		return luaL_error(L, "conv out of range");
	}
	ctx->conv = static_cast<uint32_t>(c);
	ctx->client->SetKcpConv(ctx->conv);
	return 0;
}

// ── __gc metamethod ────────────────────────────────────────────────
int l_kcp_client_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<KcpClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	ctx->client->Close();
	MEM_DELETE(ctx);

	return 0;
}

// ── Static: net.kcp_client.do_request(host, port, data, timeout_ms[, conv])  - string
int l_kcp_client_do_request_static(lua_State* L) {
	const char* host = luaL_checkstring(L, 1);
	if (!*host) {
		return luaL_error(L, "host must not be empty");
	}
	lua_Integer port64 = luaL_checkinteger(L, 2);
	if (port64 <= 0 || port64 > 65535) {
		return luaL_error(L, "port out of range");
	}
	int port = static_cast<int>(port64);
	size_t len = 0;
	const char* data = luaL_checklstring(L, 3, &len);
	lua_Integer t = luaL_optinteger(L, 4, 3000);
	if (t < 0) {
		return luaL_error(L, "timeout must be >= 0");
	}
	if (t > UINT32_MAX) {
		return luaL_error(L, "timeout too large");
	}
	auto timeout_ms = static_cast<uint32_t>(t);

	uint32_t conv = 0x11223344;
	if (lua_gettop(L) >= 5) {
		lua_Integer c = luaL_checkinteger(L, 5);
		if (c < 0 || c > UINT32_MAX) {
			return luaL_error(L, "conv out of range");
		}
		conv = static_cast<uint32_t>(c);
	}

	std::string resp =
		evpp::kcp::sync::Client::DoRequest(host, port, std::string(data, len), timeout_ms, conv);
	lua_pushlstring(L, resp.data(), resp.size());
	return 1;
}

// ── Instance method table ──────────────────────────────────────────
const luaL_Reg kKcpClientMethods[] = {
	{"connect", l_kcp_client_connect},
	{"send", l_kcp_client_send},
	{"do_request", l_kcp_client_do_request},
	{"close", l_kcp_client_close},
	{"is_connected", l_kcp_client_is_connected},
	{"set_kcp_nodelay", l_kcp_client_set_kcp_nodelay},
	{"set_kcp_wnd_size", l_kcp_client_set_kcp_wnd_size},
	{"set_kcp_mtu", l_kcp_client_set_kcp_mtu},
	{"set_kcp_conv", l_kcp_client_set_kcp_conv},
	{nullptr, nullptr},
};

// ── net.kcp_client static functions ────────────────────────────────
const luaL_Reg kKcpClientFunctions[] = {
	{"new", l_kcp_client_new},
	{"connect", l_kcp_client_connect_static},
	{"do_request", l_kcp_client_do_request_static},
	{nullptr, nullptr},
};

}  // namespace

// Public API

void RegisterKcpClientMetaTable(lua_State* L) {
	if (!L) return;

	RegisterInstanceMeta(L, kKcpClientMetaName, kKcpClientMethods, l_kcp_client_gc);
}

void PushKcpClientLibrary(lua_State* L) {
	if (!L) return;

	PushLibrary(L, kKcpClientFunctions);
}

}  // namespace script
}  // namespace engine
