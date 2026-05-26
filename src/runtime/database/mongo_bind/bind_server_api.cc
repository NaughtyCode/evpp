#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_server_api.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_server_api.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.server_api";

int l_gc(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    delete api;
    *CheckUserdata<mongo::MongoServerApi>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* api = new (std::nothrow) mongo::MongoServerApi(mongo::MongoServerApi::New(mongo::MongoServerApi::kV1));
    if (!api) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoServerApi>(L, kMetaName);
    *ud = api;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_copy(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    if (!api) { lua_pushnil(L); return 1; }
    auto* copy = new (std::nothrow) mongo::MongoServerApi(api->Copy());
    if (!copy) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoServerApi>(L, kMetaName);
    *ud = copy;
    return 1;
}

int l_set_strict(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    if (api) api->SetStrict(lua_toboolean(L, 2) != 0);
    return 0;
}

int l_set_deprecation_errors(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    if (api) api->SetDeprecationErrors(lua_toboolean(L, 2) != 0);
    return 0;
}

int l_get_strict(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    lua_pushboolean(L, api && api->GetStrict());
    return 1;
}

int l_get_deprecation_errors(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    lua_pushboolean(L, api && api->GetDeprecationErrors());
    return 1;
}

int l_get_version(lua_State* L) {
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 1, kMetaName);
    lua_pushinteger(L, api ? static_cast<lua_Integer>(api->GetVersion()) : 0);
    return 1;
}

const luaL_Reg kLib[] = {
    {"server_api_new", l_new},
    {"server_api_destroy", l_destroy},
    {"server_api_copy", l_copy},
    {"server_api_set_strict", l_set_strict},
    {"server_api_set_deprecation_errors", l_set_deprecation_errors},
    {"server_api_get_strict", l_get_strict},
    {"server_api_get_deprecation_errors", l_get_deprecation_errors},
    {"server_api_get_version", l_get_version},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoServerApiMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoServerApiLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
