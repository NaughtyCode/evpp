#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_uri.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_uri.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.uri";

int l_gc(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    delete uri;
    *CheckUserdata<mongo::MongoUri>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    const char* uri_str = luaL_checkstring(L, 1);
    auto* uri = new (std::nothrow) mongo::MongoUri(mongo::MongoUri::New(uri_str));
    if (!uri) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoUri>(L, kMetaName);
    *ud = uri;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_get_string(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetString();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_get_database(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetDatabase();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_set_database(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* db = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetDatabase(db));
    return 1;
}

int l_set_appname(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* appname = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAppname(appname));
    return 1;
}

int l_get_appname(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetAppname();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_set_username(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetUsername(val));
    return 1;
}

int l_set_password(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetPassword(val));
    return 1;
}

int l_set_auth_source(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAuthSource(val));
    return 1;
}

int l_set_auth_mechanism(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAuthMechanism(val));
    return 1;
}

int l_set_compressors(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetCompressors(val));
    return 1;
}

int l_get_option_int32(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    auto fallback = static_cast<int32_t>(luaL_optinteger(L, 3, 0));
    lua_pushinteger(L, uri ? uri->GetOptionAsInt32(opt, fallback) : fallback);
    return 1;
}

int l_set_option_int32(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    auto val = static_cast<int32_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, uri && uri->SetOptionAsInt32(opt, val));
    return 1;
}

int l_set_option_bool(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    bool val = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, uri && uri->SetOptionAsBool(opt, val));
    return 1;
}

const luaL_Reg kLib[] = {
    {"uri_new", l_new},
    {"uri_destroy", l_destroy},
    {"uri_get_string", l_get_string},
    {"uri_get_database", l_get_database},
    {"uri_set_database", l_set_database},
    {"uri_set_appname", l_set_appname},
    {"uri_get_appname", l_get_appname},
    {"uri_set_username", l_set_username},
    {"uri_set_password", l_set_password},
    {"uri_set_auth_source", l_set_auth_source},
    {"uri_set_auth_mechanism", l_set_auth_mechanism},
    {"uri_set_compressors", l_set_compressors},
    {"uri_get_option_int32", l_get_option_int32},
    {"uri_set_option_int32", l_set_option_int32},
    {"uri_set_option_bool", l_set_option_bool},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoUriMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoUriLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
