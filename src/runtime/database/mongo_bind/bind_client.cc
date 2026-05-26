#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_client.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo/mongo_session.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.client";

int l_client_gc(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (client) client->Destroy();
    delete client;
    *CheckUserdata<mongo::MongoClient>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_client_new(lua_State* L) {
    const char* uri = luaL_checkstring(L, 1);
    auto* client = mongo::MongoClient::New(uri);
    if (!client) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create client");
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoClient>(L, kMetaName);
    *ud = client;
    return 1;
}

int l_client_destroy(lua_State* L) { l_client_gc(L); return 0; }

int l_client_set_appname(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* appname = luaL_checkstring(L, 2);
    if (client) client->SetAppname(appname);
    return 0;
}

int l_client_set_socket_timeout_ms(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto timeout = static_cast<int32_t>(luaL_checkinteger(L, 2));
    if (client) client->SetSocketTimeoutMs(timeout);
    return 0;
}

int l_client_get_database(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* name = luaL_checkstring(L, 2);
    if (!client) { lua_pushnil(L); return 1; }
    auto* db = client->GetDatabase(name);
    if (!db) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoDatabase>(L, "mongoc.database");
    *ud = db;
    return 1;
}

int l_client_get_default_database(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    auto* db = client->GetDefaultDatabase();
    if (!db) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoDatabase>(L, "mongoc.database");
    *ud = db;
    return 1;
}

int l_client_get_collection(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    const char* coll_name = luaL_checkstring(L, 3);
    if (!client) { lua_pushnil(L); return 1; }
    auto* coll = client->GetCollection(db_name, coll_name);
    if (!coll) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCollection>(L, "mongoc.collection");
    *ud = coll;
    return 1;
}

int l_client_command_simple(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!client || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = client->CommandSimple(db_name, *cmd, nullptr, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) {
        lua_pushstring(L, error.Message());
        lua_pushnil(L);
    } else {
        auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
        if (!doc) {
            lua_pushnil(L);
            lua_pushnil(L);
            return 3;
        }
        lua_pushnil(L);
        auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
        *ud = doc;
    }
    return 3;
}

int l_client_start_session(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    auto* session = client->StartSession(nullptr, &error);
    if (!session) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoSession>(L, "mongoc.session");
    *ud = session;
    return 1;
}

int l_client_set_read_prefs(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (client && prefs) client->SetReadPrefs(*prefs);
    return 0;
}

int l_client_set_write_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (client && concern) client->SetWriteConcern(*concern);
    return 0;
}

int l_client_set_read_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
    if (client && concern) client->SetReadConcern(*concern);
    return 0;
}

int l_client_set_server_api(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* api = GetUserdata<mongo::MongoServerApi>(L, 2, "mongoc.server_api");
    if (!client || !api) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, client->SetServerApi(*api, &error));
    return 1;
}

int l_client_watch(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* pipeline = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!client || !pipeline) { lua_pushnil(L); return 1; }
    auto* stream = client->Watch(*pipeline, opts);
    if (!stream) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoChangeStream>(L, "mongoc.change_stream");
    *ud = stream;
    return 1;
}

int l_client_get_database_names(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    char** names = client->GetDatabaseNames(&error);
    if (!names) { lua_pushnil(L); lua_pushstring(L, error.Message()); return 2; }
    lua_newtable(L);
    int i = 1;
    for (char** p = names; *p; ++p) {
        lua_pushstring(L, *p);
        lua_rawseti(L, -2, i++);
    }
    bson_strfreev(names);
    return 1;
}

int l_client_command_with_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 4) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 4, "mongoc.read_prefs");
    auto* opts = lua_isnoneornil(L, 5) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 5, "bson.doc");
    if (!client || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = client->CommandWithOpts(db_name, *cmd, prefs, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) { lua_pushstring(L, error.Message()); lua_pushnil(L); }
    else {
        auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
        if (!doc) { lua_pushnil(L); lua_pushnil(L); return 3; }
        lua_pushnil(L);
        auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
        *ud = doc;
    }
    return 3;
}

const luaL_Reg kLib[] = {
    {"client_new", l_client_new},
    {"client_destroy", l_client_destroy},
    {"client_set_appname", l_client_set_appname},
    {"client_set_socket_timeout_ms", l_client_set_socket_timeout_ms},
    {"client_get_database", l_client_get_database},
    {"client_get_default_database", l_client_get_default_database},
    {"client_get_collection", l_client_get_collection},
    {"client_command_simple", l_client_command_simple},
    {"client_start_session", l_client_start_session},
    {"client_set_read_prefs", l_client_set_read_prefs},
    {"client_set_write_concern", l_client_set_write_concern},
    {"client_set_read_concern", l_client_set_read_concern},
    {"client_set_server_api", l_client_set_server_api},
    {"client_watch", l_client_watch},
    {"client_get_database_names", l_client_get_database_names},
    {"client_command_with_opts", l_client_command_with_opts},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoClientMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_client_gc);
}

const luaL_Reg* GetMongoClientLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
