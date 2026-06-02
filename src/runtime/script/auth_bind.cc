#include "runtime/script/auth_bind.h"

#include <map>
#include <memory>
#include <string>

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

// Token auth helpers

std::shared_ptr<auth::TokenAuthBackend> EnsureTokenBackend() {
	auto& manager = auth::SessionManager::Instance();
	if (auto token = std::dynamic_pointer_cast<auth::TokenAuthBackend>(
			manager.GetBackendSnapshot())) {
		return token;
	}

	auto backend = std::make_unique<auth::TokenAuthBackend>();
	manager.SetBackend(std::move(backend));
	return std::dynamic_pointer_cast<auth::TokenAuthBackend>(
		manager.GetBackendSnapshot());
}

std::shared_ptr<auth::AuthBackend> EnsureAuthBackend() {
	auto& manager = auth::SessionManager::Instance();
	if (auto backend = manager.GetBackendSnapshot()) {
		return backend;
	}
	return EnsureTokenBackend();
}

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

	bool failed = false;
	{
		auto backend = EnsureTokenBackend();
		if (!backend) {
			lua_pushliteral(L, "failed to initialize token auth backend");
			failed = true;
		} else {
			backend->AddToken(token, entity_id);
		}
	}
	if (failed) return lua_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// JWT auth

// auth.set_jwt_backend(secret)
int l_auth_set_jwt_backend(lua_State* L) {
	const char* secret = luaL_checkstring(L, 1);

	auto backend = std::make_unique<auth::JwtAuthBackend>();
	backend->SetSecret(secret);
	auth::SessionManager::Instance().SetBackend(std::move(backend));

	lua_pushboolean(L, 1);
	return 1;
}

// Permission management

// auth.grant_permission(entity_id, permission)
int l_auth_grant_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	bool failed = false;
	{
		auto backend = EnsureAuthBackend();
		if (!backend) {
			lua_pushliteral(L, "failed to initialize auth backend");
			failed = true;
		} else {
			backend->GrantPermission(entity_id, permission);
		}
	}
	if (failed) return lua_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// auth.revoke_permission(entity_id, permission)
int l_auth_revoke_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	bool failed = false;
	{
		auto backend = EnsureAuthBackend();
		if (!backend) {
			lua_pushliteral(L, "failed to initialize auth backend");
			failed = true;
		} else {
			backend->RevokePermission(entity_id, permission);
		}
	}
	if (failed) return lua_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// auth.has_permission(entity_id, permission) → bool
int l_auth_has_permission(lua_State* L) {
	const char* entity_id = luaL_checkstring(L, 1);
	const char* permission = luaL_checkstring(L, 2);

	bool failed = false;
	bool has = false;
	{
		auto backend = EnsureAuthBackend();
		if (!backend) {
			lua_pushliteral(L, "failed to initialize auth backend");
			failed = true;
		} else {
			has = backend->HasPermission(entity_id, permission);
		}
	}
	if (failed) return lua_error(L);

	lua_pushboolean(L, has ? 1 : 0);
	return 1;
}

// Session management

// auth.authenticate(method, params_table) → ok, entity_id, session_id | nil, err
int l_auth_authenticate(lua_State* L) {
	const char* method = luaL_checkstring(L, 1);
	bool failed = false;
	int result_count = 0;

	{
		std::map<std::string, std::string> params;

		if (lua_istable(L, 2)) {
			int table_index = lua_absindex(L, 2);
			lua_pushnil(L);
			while (lua_next(L, table_index) != 0) {
				if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
					params[lua_tostring(L, -2)] = lua_tostring(L, -1);
				}
				lua_pop(L, 1);
			}
		}

		auto backend = EnsureAuthBackend();
		if (!backend) {
			lua_pushliteral(L, "failed to initialize auth backend");
			failed = true;
		} else {
			auto result = backend->Authenticate(method, params);
			if (result.success) {
				lua_pushboolean(L, 1);
				lua_pushstring(L, result.entity_id.c_str());
				lua_pushstring(L, result.session_id.c_str());
				result_count = 3;
			} else {
				lua_pushnil(L);
				lua_pushstring(L,
							   result.reason.empty() ? "authentication failed"
													 : result.reason.c_str());
				result_count = 2;
			}
		}
	}

	if (failed) return lua_error(L);
	return result_count;
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
	auto& manager = auth::SessionManager::Instance();
	auto backend = manager.GetBackendSnapshot();
	bool valid = (backend && backend->ValidateSession(session_id)) ||
				 manager.IsSessionValid(session_id);
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

// Module registration

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
