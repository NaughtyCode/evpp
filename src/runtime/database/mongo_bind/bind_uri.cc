#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_uri.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_uri.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.uri";

int l_uri_gc(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    delete uri;
    *CheckUserdata<mongo::MongoUri>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_uri_new(lua_State* L) {
    const char* uri_str = luaL_checkstring(L, 1);
    auto* uri = new (std::nothrow) mongo::MongoUri(mongo::MongoUri::New(uri_str));
    if (!uri) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoUri>(L, kMetaName);
    *ud = uri;
    return 1;
}

int l_uri_destroy(lua_State* L) { l_uri_gc(L); return 0; }

int l_uri_get_string(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetString();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_database(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetDatabase();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_set_database(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* db = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetDatabase(db));
    return 1;
}

int l_uri_set_appname(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* appname = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAppname(appname));
    return 1;
}

int l_uri_get_appname(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetAppname();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_username(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetUsername();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_password(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetPassword();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_auth_source(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetAuthSource();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_auth_mechanism(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetAuthMechanism();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_replica_set(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    if (!uri) { lua_pushnil(L); return 1; }
    const char* s = uri->GetReplicaSet();
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_uri_get_tls(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    lua_pushboolean(L, uri && uri->GetTls());
    return 1;
}

int l_uri_has_option(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->HasOption(key));
    return 1;
}

int l_uri_set_username(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetUsername(val));
    return 1;
}

int l_uri_set_password(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetPassword(val));
    return 1;
}

int l_uri_set_auth_source(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAuthSource(val));
    return 1;
}

int l_uri_set_auth_mechanism(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetAuthMechanism(val));
    return 1;
}

int l_uri_set_compressors(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetCompressors(val));
    return 1;
}

int l_uri_set_mechanism_properties(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    auto* props = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, uri && props && uri->SetMechanismProperties(*props));
    return 1;
}

int l_uri_set_read_prefs(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (!uri || !prefs) { lua_pushboolean(L, false); return 1; }
    uri->SetReadPrefs(*prefs);
    lua_pushboolean(L, true);
    return 1;
}

int l_uri_set_write_concern(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (!uri || !concern) { lua_pushboolean(L, false); return 1; }
    uri->SetWriteConcern(*concern);
    lua_pushboolean(L, true);
    return 1;
}

int l_uri_set_read_concern(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
    if (!uri || !concern) { lua_pushboolean(L, false); return 1; }
    uri->SetReadConcern(*concern);
    lua_pushboolean(L, true);
    return 1;
}

int l_uri_set_server_monitoring_mode(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* mode = luaL_checkstring(L, 2);
    lua_pushboolean(L, uri && uri->SetServerMonitoringMode(mode));
    return 1;
}

int l_uri_get_option_int32(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    auto fallback = static_cast<int32_t>(luaL_optinteger(L, 3, 0));
    lua_pushinteger(L, uri ? uri->GetOptionAsInt32(opt, fallback) : fallback);
    return 1;
}

int l_uri_set_option_int32(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    auto val = static_cast<int32_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, uri && uri->SetOptionAsInt32(opt, val));
    return 1;
}

int l_uri_set_option_bool(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, kMetaName);
    const char* opt = luaL_checkstring(L, 2);
    bool val = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, uri && uri->SetOptionAsBool(opt, val));
    return 1;
}

const luaL_Reg kLib[] = {
    {"uri_new", l_uri_new},
    {"uri_destroy", l_uri_destroy},
    {"uri_get_string", l_uri_get_string},
    {"uri_get_database", l_uri_get_database},
    {"uri_set_database", l_uri_set_database},
    {"uri_set_appname", l_uri_set_appname},
    {"uri_get_appname", l_uri_get_appname},
    {"uri_set_username", l_uri_set_username},
    {"uri_set_password", l_uri_set_password},
    {"uri_set_auth_source", l_uri_set_auth_source},
    {"uri_set_auth_mechanism", l_uri_set_auth_mechanism},
    {"uri_set_compressors", l_uri_set_compressors},
    {"uri_get_option_int32", l_uri_get_option_int32},
    {"uri_set_option_int32", l_uri_set_option_int32},
    {"uri_set_option_bool", l_uri_set_option_bool},
    {"uri_get_username", l_uri_get_username},
    {"uri_get_password", l_uri_get_password},
    {"uri_get_auth_source", l_uri_get_auth_source},
    {"uri_get_auth_mechanism", l_uri_get_auth_mechanism},
    {"uri_get_replica_set", l_uri_get_replica_set},
    {"uri_get_tls", l_uri_get_tls},
    {"uri_has_option", l_uri_has_option},
    {"uri_set_mechanism_properties", l_uri_set_mechanism_properties},
    {"uri_set_read_prefs", l_uri_set_read_prefs},
    {"uri_set_write_concern", l_uri_set_write_concern},
    {"uri_set_read_concern", l_uri_set_read_concern},
    {"uri_set_server_monitoring_mode", l_uri_set_server_monitoring_mode},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoUriMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_uri_gc);
}

const luaL_Reg* GetMongoUriLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
