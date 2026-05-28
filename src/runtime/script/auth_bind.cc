#include "runtime/script/auth_bind.h"

#include <memory>

#include "runtime/auth/auth_backend.h"
#include "runtime/auth/session_manager.h"
#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

// auth.set_token_backend()
int l_auth_set_token_backend(lua_State* L) {
	auto backend = std::make_unique<auth::TokenAuthBackend>();
	auth::SessionManager::Instance().SetBackend(std::move(backend));
	lua_pushboolean(L, 1);
	return 1;
}

// auth.add_token(token, entity_id)
int l_auth_add_token(lua_State* L) {
	const char* token = luaL_checkstring(L, 1);
	const char* entity_id = luaL_checkstring(L, 2);

	// Access via SessionManager — create a backend if one doesn't exist.
	static auto* g_token_backend = []() -> auth::TokenAuthBackend* {
		auto b = std::make_unique<auth::TokenAuthBackend>();
		auto* ptr = b.get();
		auth::SessionManager::Instance().SetBackend(std::move(b));
		return ptr;
	}();

	g_token_backend->AddToken(token, entity_id);
	lua_pushboolean(L, 1);
	return 1;
}

// auth.authenticate(method, params_table) → ok, entity_id | nil, err
int l_auth_authenticate(lua_State* L) {
	const char* method = luaL_checkstring(L, 1);
	std::map<std::string, std::string> params;

	if (lua_istable(L, 2)) {
		lua_pushnil(L);
		while (lua_next(L, 2) != 0) {
			if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
				params[lua_tostring(L, -2)] = lua_tostring(L, -1);
			}
			lua_pop(L, 1);
		}
	}

	// Use SessionManager's internal backend for authentication.
	// The SessionManager doesn't expose Authenticate directly,
	// so we create a session via CreateSession which calls Authenticate internally.
	// For Lua-side auth, provide a direct check:
	auto result = auth::SessionManager::Instance().CreateSession(
		params.count("entity_id") ? params["entity_id"] : "", nullptr);

	if (!result.session_id.empty()) {
		lua_pushboolean(L, 1);
		lua_pushstring(L, result.session_id.c_str());
		return 2;
	}

	lua_pushboolean(L, 0);
	lua_pushstring(L, "authentication failed");
	return 2;
}

// auth.create_session(entity_id) → session_id
int l_auth_create_session(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);

	auto info = auth::SessionManager::Instance().CreateSession(entity_id, nullptr);
	if (!info.session_id.empty()) {
		lua_pushstring(L, info.session_id.c_str());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// auth.validate_session(session_id) → bool
int l_auth_validate_session(lua_State* L) {
	const char* session_id = luaL_checkstring(L, 1);
	bool valid = auth::SessionManager::Instance().IsSessionValid(session_id);
	lua_pushboolean(L, valid ? 1 : 0);
	return 1;
}

// auth.revoke_session(session_id)
int l_auth_revoke_session(lua_State* L) {
	const char* session_id = luaL_checkstring(L, 1);
	auth::SessionManager::Instance().RevokeSession(session_id);
	lua_pushboolean(L, 1);
	return 1;
}

// auth.cleanup_expired()
int l_auth_cleanup_expired(lua_State* L) {
	auth::SessionManager::Instance().CleanupExpired();
	return 0;
}

static const luaL_Reg kAuthFuncs[] = {
	{"set_token_backend", l_auth_set_token_backend},
	{"add_token",         l_auth_add_token},
	{"authenticate",      l_auth_authenticate},
	{"create_session",    l_auth_create_session},
	{"validate_session",  l_auth_validate_session},
	{"revoke_session",    l_auth_revoke_session},
	{"cleanup_expired",   l_auth_cleanup_expired},
	{nullptr, nullptr}
};

}  // namespace

void ExportAuth(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);
	for (const luaL_Reg* r = kAuthFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "auth");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Auth API exported to Lua");
}

}  // namespace script
}  // namespace engine
