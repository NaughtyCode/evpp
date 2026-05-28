#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_server_api.h"

#include <new>

#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.server_api";

int l_server_api_gc(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	MEM_DELETE(api);
	*CheckUserdata<mongo::MongoServerApi>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_server_api_new(lua_State* L) {
	auto* api = MEM_NEW_NOTHROW(mongo::MongoServerApi, mongo::MongoServerApi::New(mongo::MongoServerApi::kV1));
	if (!api) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoServerApi>(L, kMetaName);
	*ud = api;
	return 1;
}

int l_server_api_destroy(lua_State* L) {
	l_server_api_gc(L);
	return 0;
}

int l_server_api_copy(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	if (!api) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = MEM_NEW_NOTHROW(mongo::MongoServerApi, api->Copy());
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoServerApi>(L, kMetaName);
	*ud = copy;
	return 1;
}

int l_server_api_set_strict(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	if (api) api->SetStrict(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_server_api_set_deprecation_errors(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	if (api) api->SetDeprecationErrors(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_server_api_get_strict(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	lua_pushboolean(L, api && api->GetStrict());
	return 1;
}

int l_server_api_get_deprecation_errors(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	lua_pushboolean(L, api && api->GetDeprecationErrors());
	return 1;
}

int l_server_api_get_version(lua_State* L) {
	auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
	lua_pushinteger(L, api ? static_cast<lua_Integer>(api->GetVersion()) : 0);
	return 1;
}

int l_server_api_version_to_string(lua_State* L) {
	auto version = static_cast<mongo::MongoServerApi::Version>(luaL_checkinteger(L, 1));
	const char* s = mongo::MongoServerApi::VersionToString(version);
	if (s)
		lua_pushstring(L, s);
	else
		lua_pushnil(L);
	return 1;
}

int l_server_api_version_from_string(lua_State* L) {
	const char* str = luaL_checkstring(L, 1);
	mongo::MongoServerApi::Version version;
	bool ok = mongo::MongoServerApi::VersionFromString(str, &version);
	lua_pushboolean(L, ok);
	lua_pushinteger(L, ok ? static_cast<lua_Integer>(version) : 0);
	lua_pushnil(L);
	return 3;
}

const luaL_Reg kLib[] = {
	{"server_api_new", l_server_api_new},
	{"server_api_destroy", l_server_api_destroy},
	{"server_api_copy", l_server_api_copy},
	{"server_api_set_strict", l_server_api_set_strict},
	{"server_api_set_deprecation_errors", l_server_api_set_deprecation_errors},
	{"server_api_get_strict", l_server_api_get_strict},
	{"server_api_get_deprecation_errors", l_server_api_get_deprecation_errors},
	{"server_api_get_version", l_server_api_get_version},
	{"server_api_version_to_string", l_server_api_version_to_string},
	{"server_api_version_from_string", l_server_api_version_from_string},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoServerApiMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_server_api_gc);
}

const luaL_Reg* GetMongoServerApiLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
