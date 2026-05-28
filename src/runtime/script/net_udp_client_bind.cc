#include "runtime/script/net_udp_client_bind.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <memory>
#include <string>

#include <runtime/evpp/udp/sync_udp_client.h>
#include <runtime/evpp/udp/udp_message.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/script/bind_util.h"

extern "C" {
#include "runtime/config/limits.h"
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

// ======================================================================
// UDP Client bindings (light userdata + Lua class)
// ======================================================================

struct UdpClientCtx {
	std::unique_ptr<evpp::udp::sync::Client> client;
	bool connected = false;
	bool disposed = false;
};

const char* kUdpClientMetaName = "net.udp_client.instance";

// ── l_udp_client_connect(host, port) �?instance_table ──
int l_udp_client_connect(lua_State* L) {
	const char* host = luaL_checkstring(L, 1);
	lua_Integer port64 = luaL_checkinteger(L, 2);
	if (!*host) {
		return luaL_error(L, "host must not be empty");
	}
	if (port64 <= 0 || port64 > 65535) {
		return luaL_error(L, "port out of range");
	}
	int port = static_cast<int>(port64);

	auto* ctx = MEM_NEW(UdpClientCtx);

	PushInstanceTable(L, ctx, kUdpClientMetaName);

	// Create SyncUDPClient and connect
	ctx->client = std::make_unique<evpp::udp::sync::Client>();
	if (!ctx->client->Connect(host, port)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "[net.udp_client] failed to connect to {}:{}", host, port);
		ctx->disposed = true;
		// Null _ctx before delete so __gc won't read a dangling pointer
		lua_pushnil(L);
		lua_setfield(L, -2, "_ctx");
		MEM_DELETE(ctx);
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushfstring(L, "udp connect failed: %s:%d", host, port);
		return 2;
	}

	ctx->connected = true;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "[net.udp_client] connected to [{}:{}]", host, port);

	return 1;
}

// ── instance:send(data) �?bool ─────────────────────────────────────────
int l_udp_client_send(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "udp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "udp_client: closed");

	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);

		if (len > engine::ResourceLimits::kDefaultMaxMessageSize) {
			return luaL_error(L, "message size %zu exceeds limit %u",
					 len, engine::ResourceLimits::kDefaultMaxMessageSize);
		}

	bool ok = ctx->client->Send(data, len);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// ── instance:do_request(data, timeout_ms) �?string ─────────────────────
int l_udp_client_do_request(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpClientCtx>(L, 1);
	if (!ctx) return luaL_error(L, "udp_client: invalid context");
	if (ctx->disposed) return luaL_error(L, "udp_client: closed");

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

// ── instance:close() ────────────────────────────────────────────────────
int l_udp_client_close(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpClientCtx>(L, 1);
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

// ── instance:is_connected() �?bool ─────────────────────────────────────
int l_udp_client_is_connected(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpClientCtx>(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, ctx->connected ? 1 : 0);
	return 1;
}

// ── __gc metamethod ────────────────────────────────────────────────────
int l_udp_client_gc(lua_State* L) {
	auto* ctx = GetCtxFromTable<UdpClientCtx>(L, 1);
	if (!ctx || ctx->disposed) return 0;

	ctx->disposed = true;

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	ctx->client->Close();
	MEM_DELETE(ctx);

	return 0;
}

// ── Static: net.udp_client.do_request(host, port, data, timeout_ms) �?string ──
int l_udp_client_do_request_static(lua_State* L) {
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

	std::string resp =
		evpp::udp::sync::Client::DoRequest(host, port, std::string(data, len), timeout_ms);
	lua_pushlstring(L, resp.data(), resp.size());
	return 1;
}

// ── Static: net.udp_client.send_to(host, port, data) �?bool ────────────
int l_udp_client_send_to(lua_State* L) {
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

		if (len > engine::ResourceLimits::kDefaultMaxMessageSize) {
			return luaL_error(L, "message size %zu exceeds limit %u",
					 len, engine::ResourceLimits::kDefaultMaxMessageSize);
		}

	evpp::udp::sync::Client tmp;
	if (!tmp.Connect(host, port)) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(
			logger, "[net.udp_client] send_to: failed to connect to {}:{}", host, port);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "failed to connect to host");
		return 2;
	}
	bool ok = tmp.Send(data, len);
	tmp.Close();
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// ── Instance method table ──────────────────────────────────────────────
const luaL_Reg kUdpClientMethods[] = {
	{"send", l_udp_client_send},
	{"do_request", l_udp_client_do_request},
	{"close", l_udp_client_close},
	{"is_connected", l_udp_client_is_connected},
	{nullptr, nullptr},
};

// ── net.udp_client static functions ────────────────────────────────────
const luaL_Reg kUdpClientFunctions[] = {
	{"connect", l_udp_client_connect},
	{"do_request", l_udp_client_do_request_static},
	{"send_to", l_udp_client_send_to},
	{nullptr, nullptr},
};

}  // namespace

// ======================================================================
// Public API
// ======================================================================

void RegisterUdpClientMetaTable(lua_State* L) {
	if (!L) return;

	RegisterInstanceMeta(L, kUdpClientMetaName, kUdpClientMethods, l_udp_client_gc);
}

void PushUdpClientLibrary(lua_State* L) {
	if (!L) return;

	// net.udp_client table (static functions: connect, do_request, send_to)
	PushLibrary(L, kUdpClientFunctions);
}

}  // namespace script
}  // namespace engine
