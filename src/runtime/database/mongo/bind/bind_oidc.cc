#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_oidc.h"

#include <new>

#include "runtime/database/mongo/mongo_oidc.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

// MongoOidcCredential
const char* kCredMeta = "mongoc.oidc_credential";

int l_oidc_cred_gc(lua_State* L) {
	auto* cred = GetUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta);
	if (cred) {
		cred->Destroy();
		CLOUDENGINE_MEM_DELETE(cred);
	}
	*CheckUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta) = nullptr;
	return 0;
}

int l_oidc_cred_new(lua_State* L) {
	const char* token = luaL_checkstring(L, 1);
	auto* cred = mongo::MongoOidcCredential::New(token);
	if (!cred) {
		lua_pushnil(L);
		lua_pushstring(L, "credential creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoOidcCredential>(L, kCredMeta);
	*ud = cred;
	return 1;
}

int l_oidc_cred_new_with_expiry(lua_State* L) {
	const char* token = luaL_checkstring(L, 1);
	int64_t expires_in = static_cast<int64_t>(luaL_checkinteger(L, 2));
	auto* cred = mongo::MongoOidcCredential::NewWithExpiresIn(token, expires_in);
	if (!cred) {
		lua_pushnil(L);
		lua_pushstring(L, "credential creation failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoOidcCredential>(L, kCredMeta);
	*ud = cred;
	return 1;
}

int l_oidc_cred_destroy(lua_State* L) {
	l_oidc_cred_gc(L);
	return 0;
}

int l_oidc_cred_get_access_token(lua_State* L) {
	auto* cred = GetUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta);
	if (!cred) {
		lua_pushnil(L);
		return 1;
	}
	const char* token = cred->GetAccessToken();
	if (token)
		lua_pushstring(L, token);
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_cred_get_expires_in(lua_State* L) {
	auto* cred = GetUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta);
	if (!cred) {
		lua_pushnil(L);
		return 1;
	}
	const int64_t* expires = cred->GetExpiresIn();
	if (expires)
		lua_pushinteger(L, static_cast<lua_Integer>(*expires));
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_cred_get_raw(lua_State* L) {
	auto* cred = GetUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta);
	if (!cred) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = cred->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_cred_release_raw(lua_State* L) {
	auto* cred = GetUserdata<mongo::MongoOidcCredential>(L, 1, kCredMeta);
	if (!cred) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = cred->ReleaseRaw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kCredLib[] = {
	{"oidc_cred_new", l_oidc_cred_new},
	{"oidc_cred_new_with_expiry", l_oidc_cred_new_with_expiry},
	{"oidc_cred_destroy", l_oidc_cred_destroy},
	{"oidc_cred_get_access_token", l_oidc_cred_get_access_token},
	{"oidc_cred_get_expires_in", l_oidc_cred_get_expires_in},
	{"oidc_cred_get_raw", l_oidc_cred_get_raw},
	{"oidc_cred_release_raw", l_oidc_cred_release_raw},
	{nullptr, nullptr},
};


// MongoOidcCallbackParams
const char* kParamsMeta = "mongoc.oidc_callback_params";

int l_oidc_params_gc(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	CLOUDENGINE_MEM_DELETE(p);
	*CheckUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta) = nullptr;
	return 0;
}

int l_oidc_params_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid params pointer");
		lua_pushnil(L);
		return 3;
	}
	auto* p = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoOidcCallbackParams, raw);
	if (!p) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoOidcCallbackParams>(L, kParamsMeta);
	*ud = p;
	return 1;
}

int l_oidc_params_destroy(lua_State* L) {
	l_oidc_params_gc(L);
	return 0;
}

int l_oidc_params_get_version(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	lua_pushinteger(L, p ? p->GetVersion() : 0);
	return 1;
}

int l_oidc_params_get_user_data(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	if (!p) {
		lua_pushnil(L);
		return 1;
	}
	void* ud = p->GetUserData();
	if (ud)
		lua_pushlightuserdata(L, ud);
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_params_get_timeout(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	if (!p) {
		lua_pushnil(L);
		return 1;
	}
	const int64_t* timeout = p->GetTimeout();
	if (timeout)
		lua_pushinteger(L, static_cast<lua_Integer>(*timeout));
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_params_get_username(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	if (!p) {
		lua_pushnil(L);
		return 1;
	}
	const char* name = p->GetUsername();
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_params_cancel_with_timeout(lua_State* L) {
	auto* p = GetUserdata<mongo::MongoOidcCallbackParams>(L, 1, kParamsMeta);
	if (!p) {
		lua_pushnil(L);
		return 1;
	}
	auto* cred = p->CancelWithTimeout();
	if (!cred) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoOidcCredential>(L, kCredMeta);
	*ud = cred;
	return 1;
}

const luaL_Reg kParamsLib[] = {
	{"oidc_params_new", l_oidc_params_new},
	{"oidc_params_destroy", l_oidc_params_destroy},
	{"oidc_params_get_version", l_oidc_params_get_version},
	{"oidc_params_get_user_data", l_oidc_params_get_user_data},
	{"oidc_params_get_timeout", l_oidc_params_get_timeout},
	{"oidc_params_get_username", l_oidc_params_get_username},
	{"oidc_params_cancel_with_timeout", l_oidc_params_cancel_with_timeout},
	{nullptr, nullptr},
};


// MongoOidcCallback
const char* kCbMeta = "mongoc.oidc_callback";

int l_oidc_cb_gc(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoOidcCallback>(L, 1, kCbMeta);
	if (cb) cb->Destroy();
	*CheckUserdata<mongo::MongoOidcCallback>(L, 1, kCbMeta) = nullptr;
	return 0;
}

int l_oidc_cb_destroy(lua_State* L) {
	l_oidc_cb_gc(L);
	return 0;
}

int l_oidc_cb_get_user_data(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoOidcCallback>(L, 1, kCbMeta);
	if (!cb) {
		lua_pushnil(L);
		return 1;
	}
	void* ud = cb->GetUserData();
	if (ud)
		lua_pushlightuserdata(L, ud);
	else
		lua_pushnil(L);
	return 1;
}

int l_oidc_cb_set_user_data(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoOidcCallback>(L, 1, kCbMeta);
	void* data = lua_touserdata(L, 2);
	if (cb) cb->SetUserData(data);
	return 0;
}

int l_oidc_cb_get_raw(lua_State* L) {
	auto* cb = GetUserdata<mongo::MongoOidcCallback>(L, 1, kCbMeta);
	if (!cb) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = cb->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kCbLib[] = {
	{"oidc_cb_destroy", l_oidc_cb_destroy},
	{"oidc_cb_get_user_data", l_oidc_cb_get_user_data},
	{"oidc_cb_set_user_data", l_oidc_cb_set_user_data},
	{"oidc_cb_get_raw", l_oidc_cb_get_raw},
	{nullptr, nullptr},
};

}  // namespace

// ── Metatable registration functions ───────────────────────────────────────

void RegisterMongoOidcCredentialMeta(lua_State* L) {
	RegisterMetatable(L, kCredMeta, nullptr, l_oidc_cred_gc);
}
void RegisterMongoOidcCallbackParamsMeta(lua_State* L) {
	RegisterMetatable(L, kParamsMeta, nullptr, l_oidc_params_gc);
}
void RegisterMongoOidcCallbackMeta(lua_State* L) {
	RegisterMetatable(L, kCbMeta, nullptr, l_oidc_cb_gc);
}

// ── Lib getter functions ───────────────────────────────────────────────────

const luaL_Reg* GetMongoOidcCredentialLib() {
	return kCredLib;
}
const luaL_Reg* GetMongoOidcCallbackParamsLib() {
	return kParamsLib;
}
const luaL_Reg* GetMongoOidcCallbackLib() {
	return kCbLib;
}

}  // namespace script
}  // namespace engine

#endif
