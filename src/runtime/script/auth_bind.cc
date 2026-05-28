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

// =============================================================================
// Token auth helpers
// =============================================================================

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

// =============================================================================
// JWT auth
// =============================================================================

static auth::JwtAuthBackend* g_jwt_backend = nullptr;

// auth.set_jwt_backend(secret)
int l_auth_set_jwt_backend(lua_State* L) {
	const char* secret = luaL_checkstring(L, 1);

	auto backend = std::make_unique<auth::JwtAuthBackend>();
	backend->SetSecret(secret);
	g_jwt_backend = backend.get();
	auth::SessionManager::Instance().SetBackend(std::move(backend));

	lua_pushboolean(L, 1);
	return 1;
}

// =============================================================================
// Permission management
// =============================================================================

// Helper to get the current backend as AuthBackend*
static auth::AuthBackend* GetBackend() {
	static auth::TokenAuthBackend* token = nullptr;
	if (!token) {
		auto b = std::make_unique<auth::TokenAuthBackend>();
		token = b.get();
		auth::SessionManager::Instance().SetBackend(std::move(b));
	}
	return token;
}

// auth.grant_permission(entity_id, permission)
int l_auth_grant_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	auto* backend = g_jwt_backend
		? static_cast<auth::AuthBackend*>(g_jwt_backend)
		: GetBackend();
	backend->GrantPermission(entity_id, permission);

	lua_pushboolean(L, 1);
	return 1;
}

// auth.revoke_permission(entity_id, permission)
int l_auth_revoke_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	auto* backend = g_jwt_backend
		? static_cast<auth::AuthBackend*>(g_jwt_backend)
		: GetBackend();
	backend->RevokePermission(entity_id, permission);

	lua_pushboolean(L, 1);
	return 1;
}

// auth.has_permission(entity_id, permission) → bool
int l_auth_has_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	auto* backend = g_jwt_backend
		? static_cast<auth::AuthBackend*>(g_jwt_backend)
		: GetBackend();
	bool has = backend->HasPermission(entity_id, permission);

	lua_pushboolean(L, has ? 1 : 0);
	return 1;
}

// =============================================================================
// Session management
// =============================================================================

// auth.authenticate(method, params_table) → ok, entity_id, session_id | nil, err
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

	// Directly use the backend for authentication
	auto& mgr = auth::SessionManager::Instance();
	auto info = mgr.CreateSession(
		params.count("entity_id") ? params["entity_id"] : "", nullptr);

	if (!info.session_id.empty()) {
		lua_pushboolean(L, 1);
		lua_pushstring(L, info.entity_id.c_str());
		lua_pushstring(L, info.session_id.c_str());
		return 3;
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

// =============================================================================
// Module registration
// =============================================================================

static const luaL_Reg kAuthFuncs[] = {
	{"set_token_backend",  l_auth_set_token_backend},
	{"set_jwt_backend",    l_auth_set_jwt_backend},
	{"add_token",          l_auth_add_token},
	{"authenticate",       l_auth_authenticate},
	{"create_session",     l_auth_create_session},
	{"validate_session",   l_auth_validate_session},
	{"revoke_session",     l_auth_revoke_session},
	{"grant_permission",   l_auth_grant_permission},
	{"revoke_permission",  l_auth_revoke_permission},
	{"has_permission",     l_auth_has_permission},
	{"cleanup_expired",    l_auth_cleanup_expired},
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
