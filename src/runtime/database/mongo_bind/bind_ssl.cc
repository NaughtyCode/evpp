#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_ssl.h"

#include <new>

#include "runtime/database/mongo/mongo_ssl.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.ssl_opts";

int l_ssl_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	delete opts;
	*CheckUserdata<mongo::MongoSslOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_ssl_opts_new(lua_State* L) {
	auto* opts = new (std::nothrow) mongo::MongoSslOpts();
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoSslOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_ssl_opts_destroy(lua_State* L) {
	l_ssl_opts_gc(L);
	return 0;
}

int l_ssl_opts_set_pem_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	const char* val = luaL_checkstring(L, 2);
	if (opts) opts->SetPemFile(val);
	return 0;
}

int l_ssl_opts_get_pem_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	const char* s = opts->GetPemFile();
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_set_pem_pwd(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	const char* val = luaL_checkstring(L, 2);
	if (opts) opts->SetPemPwd(val);
	return 0;
}

int l_ssl_opts_get_pem_pwd(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	const char* s = opts->GetPemPwd();
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_set_ca_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	const char* val = luaL_checkstring(L, 2);
	if (opts) opts->SetCaFile(val);
	return 0;
}

int l_ssl_opts_get_ca_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	const char* s = opts->GetCaFile();
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_set_ca_dir(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	const char* val = luaL_checkstring(L, 2);
	if (opts) opts->SetCaDir(val);
	return 0;
}

int l_ssl_opts_get_ca_dir(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	const char* s = opts->GetCaDir();
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_set_crl_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	const char* val = luaL_checkstring(L, 2);
	if (opts) opts->SetCrlFile(val);
	return 0;
}

int l_ssl_opts_get_crl_file(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	const char* s = opts->GetCrlFile();
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_set_weak_cert_validation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (opts) opts->SetWeakCertValidation(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_ssl_opts_get_weak_cert_validation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	lua_pushboolean(L, opts && opts->GetWeakCertValidation());
	return 1;
}

int l_ssl_opts_set_allow_invalid_hostname(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (opts) opts->SetAllowInvalidHostname(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_ssl_opts_get_allow_invalid_hostname(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	lua_pushboolean(L, opts && opts->GetAllowInvalidHostname());
	return 1;
}

int l_ssl_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoSslOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

int l_ssl_opts_get_default(lua_State* L) {
	const void* def = mongo::MongoSslOpts::GetDefault();
	if (def)
		lua_pushlightuserdata(L, const_cast<void*>(def));
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kLib[] = {
	{"ssl_opts_new", l_ssl_opts_new},
	{"ssl_opts_destroy", l_ssl_opts_destroy},
	{"ssl_opts_set_pem_file", l_ssl_opts_set_pem_file},
	{"ssl_opts_get_pem_file", l_ssl_opts_get_pem_file},
	{"ssl_opts_set_pem_pwd", l_ssl_opts_set_pem_pwd},
	{"ssl_opts_get_pem_pwd", l_ssl_opts_get_pem_pwd},
	{"ssl_opts_set_ca_file", l_ssl_opts_set_ca_file},
	{"ssl_opts_get_ca_file", l_ssl_opts_get_ca_file},
	{"ssl_opts_set_ca_dir", l_ssl_opts_set_ca_dir},
	{"ssl_opts_get_ca_dir", l_ssl_opts_get_ca_dir},
	{"ssl_opts_set_crl_file", l_ssl_opts_set_crl_file},
	{"ssl_opts_get_crl_file", l_ssl_opts_get_crl_file},
	{"ssl_opts_set_weak_cert_validation", l_ssl_opts_set_weak_cert_validation},
	{"ssl_opts_get_weak_cert_validation", l_ssl_opts_get_weak_cert_validation},
	{"ssl_opts_set_allow_invalid_hostname", l_ssl_opts_set_allow_invalid_hostname},
	{"ssl_opts_get_allow_invalid_hostname", l_ssl_opts_get_allow_invalid_hostname},
	{"ssl_opts_get_raw", l_ssl_opts_get_raw},
	{"ssl_opts_get_default", l_ssl_opts_get_default},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoSslOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_ssl_opts_gc);
}

const luaL_Reg* GetMongoSslOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
