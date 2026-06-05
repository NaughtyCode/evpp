#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_socket.h"

#include <cstring>
#include <new>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_iovec.h"
#include "runtime/database/mongo/mongo_socket.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.socket";

// ── GC / Destroy ──────────────────────────────────────────────────────────

int l_socket_gc(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	if (s) s->Destroy();
	*CheckUserdata<mongo::MongoSocket>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_socket_destroy(lua_State* L) {
	l_socket_gc(L);
	return 0;
}

// ── Factory ───────────────────────────────────────────────────────────────

int l_socket_new(lua_State* L) {
	int domain = CheckIntegerArg<int>(L, 1);
	int type = CheckIntegerArg<int>(L, 2);
	int protocol = CheckIntegerArg<int>(L, 3);
	auto* s = mongo::MongoSocket::New(domain, type, protocol);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "socket creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoSocket>(L, kMetaName);
	*ud = s;
	return 1;
}

// ── Instance methods ──────────────────────────────────────────────────────

int l_socket_accept(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	int64_t expire_at = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	auto* accepted = s->Accept(expire_at);
	if (!accepted) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoSocket>(L, kMetaName);
	*ud = accepted;
	return 1;
}

int l_socket_bind(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	const char* ip = luaL_checkstring(L, 2);
	const auto port = CheckIntegerArg<uint16_t>(L, 3);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &addr.sin_addr);
	int rc = s->Bind(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
	lua_pushinteger(L, rc);
	return 1;
}

int l_socket_close(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	lua_pushinteger(L, s ? s->Close() : -1);
	return 1;
}

int l_socket_connect(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	const char* ip = luaL_checkstring(L, 2);
	const auto port = CheckIntegerArg<uint16_t>(L, 3);
	int64_t expire_at = static_cast<int64_t>(luaL_checkinteger(L, 4));
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &addr.sin_addr);
	int rc = s->Connect(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr), expire_at);
	lua_pushinteger(L, rc);
	return 1;
}

int l_socket_get_name_info(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		return 1;
	}
	char* info = s->GetNameInfo();
	if (info) {
		lua_pushstring(L, info);
		bson_free(info);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_socket_get_sock_name(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	struct sockaddr_storage addr;
	memset(&addr, 0, sizeof(addr));
	int addrlen = sizeof(addr);
	int rc = s->GetSockName(reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
	if (rc != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "getsockname failed");
		lua_pushnil(L);
		return 3;
	}
	char ip[INET6_ADDRSTRLEN];
	int port = 0;
	if (addr.ss_family == AF_INET) {
		auto* sa = reinterpret_cast<struct sockaddr_in*>(&addr);
		inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
		port = ntohs(sa->sin_port);
	} else if (addr.ss_family == AF_INET6) {
		auto* sa6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
		inet_ntop(AF_INET6, &sa6->sin6_addr, ip, sizeof(ip));
		port = ntohs(sa6->sin6_port);
	} else {
		lua_pushnil(L);
		lua_pushstring(L, "unknown address family");
		lua_pushnil(L);
		return 3;
	}
	lua_pushstring(L, ip);
	lua_pushinteger(L, port);
	lua_pushnil(L);
	return 3;
}

int l_socket_get_error(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	lua_pushinteger(L, s ? s->GetError() : 0);
	return 1;
}

int l_socket_listen(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	auto backlog = CheckIntegerArg<unsigned int>(L, 2);
	lua_pushinteger(L, s ? s->Listen(backlog) : -1);
	return 1;
}

int l_socket_receive(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	auto buf_size = CheckIntegerArg<size_t>(L, 2);
	int flags = OptIntegerArg<int>(L, 3, 0);
	int64_t expire_at = static_cast<int64_t>(luaL_optinteger(L, 4, 0));
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	std::vector<char> buf(buf_size);
	ssize_t n = s->Receive(buf.data(), buf_size, flags, expire_at);
	if (n < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, buf.data(), static_cast<size_t>(n));
	return 1;
}

int l_socket_send(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	int64_t expire_at = static_cast<int64_t>(luaL_checkinteger(L, 3));
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	ssize_t sent = s->SendData(data, len, expire_at);
	lua_pushinteger(L, static_cast<lua_Integer>(sent));
	return 1;
}

int l_socket_sendv(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	int64_t expire_at = static_cast<int64_t>(luaL_checkinteger(L, 3));
	int n = CheckLengthArg<int>(L, 2);
	std::vector<mongo::MongoIovec> iov(static_cast<size_t>(n));
	std::vector<std::vector<char>> buffers(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 2, i + 1);
		size_t len;
		const char* data = luaL_checklstring(L, -1, &len);
		buffers[i].assign(data, data + len);
		iov[i].iov_base = buffers[i].data();
		iov[i].iov_len = len;
		lua_pop(L, 1);
	}
	ssize_t sent = s->SendvData(iov.data(), static_cast<size_t>(n), expire_at);
	lua_pushinteger(L, static_cast<lua_Integer>(sent));
	return 1;
}

int l_socket_set_sockopt(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	int level = CheckIntegerArg<int>(L, 2);
	int optname = CheckIntegerArg<int>(L, 3);
	int optval = CheckIntegerArg<int>(L, 4);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	int rc = s->SetSockOpt(level, optname, &optval, sizeof(optval));
	lua_pushinteger(L, rc);
	return 1;
}

int l_socket_check_closed(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	lua_pushboolean(L, s && s->CheckClosed());
	return 1;
}

int l_socket_get_raw(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoSocket>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = s->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

// ── Static PollFds ────────────────────────────────────────────────────────

int l_socket_poll_fds(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	auto timeout = CheckIntegerArg<int32_t>(L, 2);
	int n = CheckLengthArg<int>(L, 1);

	std::vector<mongo::MongoSocketPollFd> fds(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 1, i + 1);
		luaL_checktype(L, -1, LUA_TTABLE);

		lua_getfield(L, -1, "socket");
		fds[i].socket = GetUserdata<mongo::MongoSocket>(L, -1, kMetaName);
		lua_pop(L, 1);

		lua_getfield(L, -1, "events");
		fds[i].events = CheckIntegerArg<int>(L, -1);
		fds[i].revents = 0;
		lua_pop(L, 1);

		lua_pop(L, 1);	// pop the inner table
	}

	ssize_t rc = mongo::MongoSocket::PollFds(fds.data(), static_cast<size_t>(n), timeout);

	// Write revents back into each inner table
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 1, i + 1);
		lua_pushinteger(L, fds[i].revents);
		lua_setfield(L, -2, "revents");
		lua_pop(L, 1);
	}

	lua_pushinteger(L, static_cast<lua_Integer>(rc));
	return 1;
}

// ── Library table ─────────────────────────────────────────────────────────

const luaL_Reg kLib[] = {
	{"socket_new", l_socket_new},
	{"socket_destroy", l_socket_destroy},
	{"socket_accept", l_socket_accept},
	{"socket_bind", l_socket_bind},
	{"socket_close", l_socket_close},
	{"socket_connect", l_socket_connect},
	{"socket_get_name_info", l_socket_get_name_info},
	{"socket_get_sock_name", l_socket_get_sock_name},
	{"socket_get_error", l_socket_get_error},
	{"socket_listen", l_socket_listen},
	{"socket_receive", l_socket_receive},
	{"socket_send", l_socket_send},
	{"socket_sendv", l_socket_sendv},
	{"socket_set_sockopt", l_socket_set_sockopt},
	{"socket_check_closed", l_socket_check_closed},
	{"socket_get_raw", l_socket_get_raw},
	{"socket_poll_fds", l_socket_poll_fds},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoSocketMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_socket_gc);
}

const luaL_Reg* GetMongoSocketLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
