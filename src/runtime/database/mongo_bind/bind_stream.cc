#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_stream.h"

#include <cstring>
#include <new>
#include <vector>

#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_stream.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.stream";

// Simple iovec struct for Readv/Writev
struct SimpleIovec {
	void* iov_base;
	size_t iov_len;
};

// ── GC / Destroy ──────────────────────────────────────────────────────────

int l_stream_gc(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (s) s->Destroy();
	*CheckUserdata<mongo::MongoStream>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_stream_destroy(lua_State* L) {
	l_stream_gc(L);
	return 0;
}

// ── Factory methods ───────────────────────────────────────────────────────

int l_stream_new_buffered(lua_State* L) {
	auto* base = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	auto buf_size = CheckIntegerArg<size_t>(L, 2);
	if (!base) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid base stream");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewBuffered(base, buf_size);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_file(lua_State* L) {
	int fd = CheckIntegerArg<int>(L, 1);
	auto* s = mongo::MongoStream::NewFile(fd);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_file_for_path(lua_State* L) {
	const char* path = luaL_checkstring(L, 1);
	int flags = CheckIntegerArg<int>(L, 2);
	int mode = CheckIntegerArg<int>(L, 3);
	auto* s = mongo::MongoStream::NewFileForPath(path, flags, mode);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_socket(lua_State* L) {
	void* socket = lua_touserdata(L, 1);
	if (!socket) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid socket");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewSocket(socket);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_tls(lua_State* L) {
	auto* base = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	void* ssl_opts = lua_touserdata(L, 3);
	bool client = lua_toboolean(L, 4) != 0;
	if (!base) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid base stream");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewTls(base, host, ssl_opts, client ? 1 : 0);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_gridfs(lua_State* L) {
	void* gridfs_file = lua_touserdata(L, 1);
	auto* s = mongo::MongoStream::NewGridFs(gridfs_file);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_tls_openssl(lua_State* L) {
	auto* base = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	void* ssl_opts = lua_touserdata(L, 3);
	int client = lua_toboolean(L, 4) != 0 ? 1 : 0;
	if (!base) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid base stream");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewTlsOpenssl(base, host, ssl_opts, client);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_tls_secure_channel(lua_State* L) {
	auto* base = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	void* ssl_opts = lua_touserdata(L, 3);
	int client = lua_toboolean(L, 4) != 0 ? 1 : 0;
	if (!base) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid base stream");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewTlsSecureChannel(base, host, ssl_opts, client);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

int l_stream_new_tls_secure_transport(lua_State* L) {
	auto* base = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	void* ssl_opts = lua_touserdata(L, 3);
	int client = lua_toboolean(L, 4) != 0 ? 1 : 0;
	if (!base) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid base stream");
		lua_pushnil(L);
		return 3;
	}
	auto* s = mongo::MongoStream::NewTlsSecureTransport(base, host, ssl_opts, client);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "stream creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoStream>(L, kMetaName);
	*ud = s;
	return 1;
}

// ── Non-owning stream accessors ───────────────────────────────────────────

int l_stream_get_base_stream(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		return 1;
	}
	auto* base = s->GetBaseStream();
	if (!base) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, base->Raw());
	return 1;
}

int l_stream_get_tls_stream(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		return 1;
	}
	auto* tls = s->GetTlsStream();
	if (!tls) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, tls->Raw());
	return 1;
}

// ── Base stream operations ────────────────────────────────────────────────

int l_stream_close(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushinteger(L, s ? s->Close() : -1);
	return 1;
}

int l_stream_failed(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (s) s->Failed();
	return 0;
}

int l_stream_flush(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushinteger(L, s ? s->Flush() : -1);
	return 1;
}

int l_stream_write(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	auto timeout = CheckIntegerArg<int32_t>(L, 3);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	ssize_t written = s->Write(const_cast<char*>(data), len, timeout);
	lua_pushinteger(L, static_cast<lua_Integer>(written));
	return 1;
}

int l_stream_read(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	auto count = CheckIntegerArg<size_t>(L, 2);
	auto min_bytes = OptIntegerArg<size_t>(L, 3, 0);
	auto timeout = OptIntegerArg<int32_t>(L, 4, 0);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	if (count == 0) {
		lua_pushstring(L, "");
		return 1;
	}
	std::vector<char> buf(count);
	ssize_t nread = s->Read(buf.data(), count, min_bytes, timeout);
	if (nread < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, buf.data(), static_cast<size_t>(nread));
	return 1;
}

int l_stream_writev(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	auto timeout = CheckIntegerArg<int32_t>(L, 3);
	int n = CheckLengthArg<int>(L, 2);
	luaL_checkstack(L, n, "too many buffers");
	std::vector<SimpleIovec> iov(static_cast<size_t>(n));
	// Keep string references on the stack during Writev to prevent GC
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 2, i + 1);
		size_t len;
		const char* data = luaL_tolstring(L, -1, &len);
		lua_remove(L, -2);
		iov[i].iov_base = const_cast<char*>(data);
		iov[i].iov_len = len;
	}
	ssize_t written = s->Writev(iov.data(), static_cast<size_t>(n), timeout);
	lua_pop(L, n);
	lua_pushinteger(L, static_cast<lua_Integer>(written));
	return 1;
}

int l_stream_readv(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	auto min_bytes = OptIntegerArg<size_t>(L, 3, 0);
	auto timeout = OptIntegerArg<int32_t>(L, 4, 0);
	int n = CheckLengthArg<int>(L, 2);
	std::vector<SimpleIovec> iov(static_cast<size_t>(n));
	std::vector<std::vector<char>> bufs(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 2, i + 1);
		size_t len;
		luaL_checklstring(L, -1, &len);
		bufs[i].resize(len);
		std::memcpy(bufs[i].data(), lua_tostring(L, -1), len);
		iov[i].iov_base = bufs[i].data();
		iov[i].iov_len = len;
		lua_pop(L, 1);
	}
	ssize_t nread = s->Readv(iov.data(), static_cast<size_t>(n), min_bytes, timeout);
	if (nread < 0) {
		lua_pushnil(L);
		return 1;
	}
	// Write filled buffers back into the table
	for (int i = 0; i < n; ++i) {
		lua_pushlstring(L, bufs[i].data(), iov[i].iov_len);
		lua_rawseti(L, 2, i + 1);
	}
	lua_pushinteger(L, static_cast<lua_Integer>(nread));
	return 1;
}

int l_stream_set_sockopt(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	int level = CheckIntegerArg<int>(L, 2);
	int optname = CheckIntegerArg<int>(L, 3);
	size_t optlen;
	const char* optval = luaL_checklstring(L, 4, &optlen);
	if (optlen > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		return luaL_argerror(L, 4, "option value too large");
	}
	if (!s) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	int rc = s->SetSockopt(level, optname, const_cast<char*>(optval), static_cast<int>(optlen));
	lua_pushinteger(L, rc);
	return 1;
}

int l_stream_check_closed(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushboolean(L, s && s->CheckClosed());
	return 1;
}

int l_stream_timed_out(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushboolean(L, s && s->TimedOut());
	return 1;
}

int l_stream_should_retry(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushboolean(L, s && s->ShouldRetry());
	return 1;
}

// ── TLS operations ────────────────────────────────────────────────────────

int l_stream_tls_handshake(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	auto timeout = CheckIntegerArg<int32_t>(L, 3);
	if (!s) {
		lua_pushboolean(L, false);
		lua_pushinteger(L, 0);
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	int events = 0;
	bool ok = s->TlsHandshake(host, timeout, &events, &error);
	lua_pushboolean(L, ok);
	if (ok) {
		lua_pushinteger(L, events);
	} else {
		lua_pushstring(L, error.Message() ? error.Message() : "TLS handshake failed");
	}
	lua_pushnil(L);
	return 3;
}

int l_stream_tls_handshake_block(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	const char* host = luaL_checkstring(L, 2);
	auto timeout = CheckIntegerArg<int32_t>(L, 3);
	if (!s) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid stream");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	bool ok = s->TlsHandshakeBlock(host, timeout, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message() ? error.Message() : "TLS handshake failed");
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

// ── File / Socket / Raw accessors ─────────────────────────────────────────

int l_stream_get_file_fd(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	lua_pushinteger(L, s ? s->GetFileFd() : -1);
	return 1;
}

int l_stream_get_socket(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
	if (!s) {
		lua_pushnil(L);
		return 1;
	}
	void* socket = s->GetSocket();
	if (socket)
		lua_pushlightuserdata(L, socket);
	else
		lua_pushnil(L);
	return 1;
}

int l_stream_get_raw(lua_State* L) {
	auto* s = GetUserdata<mongo::MongoStream>(L, 1, kMetaName);
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

// ── Static Poll ───────────────────────────────────────────────────────────

int l_stream_poll(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	auto timeout = CheckIntegerArg<int32_t>(L, 2);
	int n = CheckLengthArg<int>(L, 1);

	// Build array of mongoc_stream_poll_t-compatible structs
	struct StreamPollFd {
		void* stream;
		int events;
		int revents;
	};
	std::vector<StreamPollFd> fds(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		lua_rawgeti(L, 1, i + 1);
		fds[i].stream = lua_touserdata(L, -1);
		fds[i].events = 0;
		fds[i].revents = 0;
		lua_pop(L, 1);
	}
	ssize_t rc = mongo::MongoStream::Poll(fds.data(), static_cast<size_t>(n), timeout);
	lua_pushinteger(L, static_cast<lua_Integer>(rc));
	return 1;
}

// ── Library table ─────────────────────────────────────────────────────────

const luaL_Reg kLib[] = {
	{"stream_new_buffered", l_stream_new_buffered},
	{"stream_new_file", l_stream_new_file},
	{"stream_new_file_for_path", l_stream_new_file_for_path},
	{"stream_new_socket", l_stream_new_socket},
	{"stream_new_tls", l_stream_new_tls},
	{"stream_new_gridfs", l_stream_new_gridfs},
	{"stream_new_tls_openssl", l_stream_new_tls_openssl},
	{"stream_new_tls_secure_channel", l_stream_new_tls_secure_channel},
	{"stream_new_tls_secure_transport", l_stream_new_tls_secure_transport},
	{"stream_destroy", l_stream_destroy},
	{"stream_get_base_stream", l_stream_get_base_stream},
	{"stream_get_tls_stream", l_stream_get_tls_stream},
	{"stream_close", l_stream_close},
	{"stream_failed", l_stream_failed},
	{"stream_flush", l_stream_flush},
	{"stream_write", l_stream_write},
	{"stream_read", l_stream_read},
	{"stream_writev", l_stream_writev},
	{"stream_readv", l_stream_readv},
	{"stream_set_sockopt", l_stream_set_sockopt},
	{"stream_check_closed", l_stream_check_closed},
	{"stream_timed_out", l_stream_timed_out},
	{"stream_should_retry", l_stream_should_retry},
	{"stream_tls_handshake", l_stream_tls_handshake},
	{"stream_tls_handshake_block", l_stream_tls_handshake_block},
	{"stream_get_file_fd", l_stream_get_file_fd},
	{"stream_get_socket", l_stream_get_socket},
	{"stream_get_raw", l_stream_get_raw},
	{"stream_poll", l_stream_poll},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoStreamMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_stream_gc);
}

const luaL_Reg* GetMongoStreamLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
