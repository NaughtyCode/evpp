#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_client_pool.h"

#include <new>

#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo/mongo_uri.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.pool";

int l_pool_gc(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	if (pool) pool->Destroy();
	delete pool;
	*CheckUserdata<mongo::MongoClientPool>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_pool_new(lua_State* L) {
	const char* uri_str = luaL_checkstring(L, 1);
	auto uri = mongo::MongoUri::New(uri_str);
	auto* pool = mongo::MongoClientPool::New(uri);
	if (!pool) {
		lua_pushnil(L);
		lua_pushstring(L, "failed to create client pool");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientPool>(L, kMetaName);
	*ud = pool;
	return 1;
}

int l_pool_new_with_error(lua_State* L) {
	const char* uri_str = luaL_checkstring(L, 1);
	auto uri = mongo::MongoUri::New(uri_str);
	mongo::MongoError error;
	auto* pool = mongo::MongoClientPool::New(uri, &error);
	if (!pool) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClientPool>(L, kMetaName);
	*ud = pool;
	return 1;
}

int l_pool_destroy(lua_State* L) {
	l_pool_gc(L);
	return 0;
}

int l_pool_pop(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	if (!pool) {
		lua_pushnil(L);
		return 1;
	}
	auto* client = pool->Pop();
	if (!client) {
		lua_pushnil(L);
		lua_pushstring(L, "pool pop failed");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoClient>(L, "mongoc.client");
	*ud = client;
	return 1;
}

int l_pool_push(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	auto* client = GetUserdata<mongo::MongoClient>(L, 2, "mongoc.client");
	if (pool && client) pool->Push(client);
	return 0;
}

int l_pool_set_max_size(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	auto size = static_cast<uint32_t>(luaL_checkinteger(L, 2));
	if (pool) pool->SetMaxSize(size);
	return 0;
}

int l_pool_set_appname(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	const char* appname = luaL_checkstring(L, 2);
	if (pool) pool->SetAppname(appname);
	return 0;
}

int l_pool_try_pop(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	if (!pool) {
		lua_pushnil(L);
		return 1;
	}
	auto* client = pool->TryPop();
	if (!client) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoClient>(L, "mongoc.client");
	*ud = client;
	return 1;
}

int l_pool_set_ssl_opts(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	const void* ssl_opts = lua_touserdata(L, 2);
	if (pool) pool->SetSslOpts(ssl_opts);
	return 0;
}

int l_pool_set_apm_callbacks(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	void* callbacks = lua_touserdata(L, 2);
	void* context = lua_touserdata(L, 3);
	lua_pushboolean(L, pool && pool->SetApmCallbacks(callbacks, context));
	return 1;
}

int l_pool_set_error_api(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	auto version = static_cast<uint32_t>(luaL_checkinteger(L, 2));
	lua_pushboolean(L, pool && pool->SetErrorApi(version));
	return 1;
}

int l_pool_set_server_api(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 2, "mongoc.server_api");
	if (!pool || !api) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, pool->SetServerApi(*api, &error));
	return 1;
}

int l_pool_append_metadata(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	const char* name = luaL_checkstring(L, 2);
	const char* version = luaL_checkstring(L, 3);
	const char* platform = luaL_checkstring(L, 4);
	lua_pushboolean(L, pool && pool->AppendMetadata(name, version, platform));
	return 1;
}

int l_pool_enable_auto_encryption(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	void* opts = lua_touserdata(L, 2);
	if (!pool) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, pool->EnableAutoEncryption(opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_pool_set_structured_log_opts(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	const void* opts = lua_touserdata(L, 2);
	lua_pushboolean(L, pool && pool->SetStructuredLogOpts(opts));
	return 1;
}

int l_pool_set_oidc_callback(lua_State* L) {
	auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
	const void* callback = lua_touserdata(L, 2);
	lua_pushboolean(L, pool && pool->SetOidcCallback(callback));
	return 1;
}

const luaL_Reg kLib[] = {
	{"pool_new", l_pool_new},
	{"pool_new_with_error", l_pool_new_with_error},
	{"pool_destroy", l_pool_destroy},
	{"pool_pop", l_pool_pop},
	{"pool_try_pop", l_pool_try_pop},
	{"pool_push", l_pool_push},
	{"pool_set_max_size", l_pool_set_max_size},
	{"pool_set_appname", l_pool_set_appname},
	{"pool_set_ssl_opts", l_pool_set_ssl_opts},
	{"pool_set_apm_callbacks", l_pool_set_apm_callbacks},
	{"pool_set_error_api", l_pool_set_error_api},
	{"pool_set_server_api", l_pool_set_server_api},
	{"pool_append_metadata", l_pool_append_metadata},
	{"pool_enable_auto_encryption", l_pool_enable_auto_encryption},
	{"pool_set_structured_log_opts", l_pool_set_structured_log_opts},
	{"pool_set_oidc_callback", l_pool_set_oidc_callback},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoClientPoolMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_pool_gc);
}

const luaL_Reg* GetMongoClientPoolLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
