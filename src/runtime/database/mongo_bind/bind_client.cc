#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_client.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo/mongo_session.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo/mongo_uri.h"

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

int l_client_new_from_uri(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, "mongoc.uri");
    auto* client = mongo::MongoClient::New(*uri);
    if (!client) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create client");
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoClient>(L, kMetaName);
    *ud = client;
    return 1;
}

int l_client_new_from_uri_with_error(lua_State* L) {
    auto* uri = GetUserdata<mongo::MongoUri>(L, 1, "mongoc.uri");
    mongo::MongoError error;
    auto* client = mongo::MongoClient::New(*uri, &error);
    if (!client) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
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
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                : GetUserdata<mongo::MongoSessionOpts>(L, 2, "mongoc.session_opts");
    mongo::MongoError error;
    auto* session = client->StartSession(opts, &error);
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

int l_client_get_read_prefs(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    const void* prefs = client->GetReadPrefs();
    if (!prefs) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, const_cast<void*>(prefs));
    return 1;
}

int l_client_set_write_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (client && concern) client->SetWriteConcern(*concern);
    return 0;
}

int l_client_get_write_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    const void* concern = client->GetWriteConcern();
    if (!concern) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, const_cast<void*>(concern));
    return 1;
}

int l_client_set_read_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
    if (client && concern) client->SetReadConcern(*concern);
    return 0;
}

int l_client_get_read_concern(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    const void* concern = client->GetReadConcern();
    if (!concern) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, const_cast<void*>(concern));
    return 1;
}

int l_client_get_uri(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    auto* uri = new (std::nothrow) mongo::MongoUri(client->GetUri());
    if (!uri) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoUri>(L, "mongoc.uri");
    *ud = uri;
    return 1;
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

int l_client_read_command_with_opts(lua_State* L) {
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
    bool ok = client->ReadCommandWithOpts(db_name, *cmd, prefs, opts, &reply, &error);
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

int l_client_write_command_with_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!client || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = client->WriteCommandWithOpts(db_name, *cmd, opts, &reply, &error);
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

int l_client_read_write_command_with_opts(lua_State* L) {
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
    bool ok = client->ReadWriteCommandWithOpts(db_name, *cmd, prefs, opts, &reply, &error);
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

int l_client_command_simple_with_server_id(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 4) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 4, "mongoc.read_prefs");
    auto server_id = static_cast<uint32_t>(luaL_checkinteger(L, 5));
    if (!client || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = client->CommandSimpleWithServerId(db_name, *cmd, prefs, server_id, &reply, &error);
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

int l_client_set_ssl_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    // ssl_opts passed as lightuserdata (mongoc_ssl_opt_t*)
    const void* ssl_opts = lua_touserdata(L, 2);
    if (client) client->SetSslOpts(ssl_opts);
    return 0;
}

int l_client_set_error_api(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto version = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (client) client->SetErrorApi(version);
    return 0;
}

int l_client_reset(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (client) client->Reset();
    return 0;
}

int l_client_release_from_pool(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (client) client->ReleaseFromPool();
    return 0;
}

int l_client_from_pooled(lua_State* L) {
    void* raw_client = lua_touserdata(L, 1);
    if (!raw_client) { lua_pushnil(L); lua_pushstring(L, "raw client pointer required"); return 2; }
    auto* client = mongo::MongoClient::FromPooled(raw_client);
    if (!client) { lua_pushnil(L); lua_pushstring(L, "failed to wrap pooled client"); return 2; }
    auto** ud = NewUserdata<mongo::MongoClient>(L, kMetaName);
    *ud = client;
    return 1;
}

int l_client_get_database_names_with_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!client) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    char** names = client->GetDatabaseNamesWithOpts(opts, &error);
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

int l_client_find_databases_with_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!client) { lua_pushnil(L); return 1; }
    auto* cursor = client->FindDatabasesWithOpts(opts);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

int l_client_set_apm_callbacks(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    // callbacks and context passed as lightuserdata
    void* callbacks = lua_touserdata(L, 2);
    void* context = lua_touserdata(L, 3);
    lua_pushboolean(L, client && client->SetApmCallbacks(callbacks, context));
    return 1;
}

int l_client_set_structured_log_opts(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const void* opts = lua_touserdata(L, 2);
    lua_pushboolean(L, client && client->SetStructuredLogOpts(opts));
    return 1;
}

int l_client_set_oidc_callback(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const void* callback = lua_touserdata(L, 2);
    lua_pushboolean(L, client && client->SetOidcCallback(callback));
    return 1;
}

int l_client_enable_auto_encryption(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    void* opts = lua_touserdata(L, 2);
    if (!client) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, client->EnableAutoEncryption(opts, &error));
    if (!lua_toboolean(L, -1)) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_client_append_metadata(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* name = luaL_checkstring(L, 2);
    const char* version = luaL_checkstring(L, 3);
    const char* platform = luaL_checkstring(L, 4);
    lua_pushboolean(L, client && client->AppendMetadata(name, version, platform));
    return 1;
}

int l_client_get_gridfs(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    const char* db = luaL_checkstring(L, 2);
    const char* prefix = luaL_optstring(L, 3, "fs");
    if (!client) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    void* raw_gridfs = client->GetGridfs(db, prefix, &error);
    if (!raw_gridfs) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    // Return as lightuserdata — caller wraps with gridfs binding if available
    lua_pushlightuserdata(L, raw_gridfs);
    return 1;
}

int l_client_select_server(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    bool for_writes = lua_toboolean(L, 2);
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    if (!client) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::MongoError error;
    void* server_id = client->SelectServer(for_writes, prefs, &error);
    if (!server_id) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    lua_pushlightuserdata(L, server_id);
    return 1;
}

int l_client_get_server_description(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto server_id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (!client) { lua_pushnil(L); return 1; }
    void* desc = client->GetServerDescription(server_id);
    if (!desc) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, desc);
    return 1;
}

int l_client_get_server_descriptions(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    size_t n = 0;
    void** sds = client->GetServerDescriptions(&n);
    if (!sds) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    for (size_t i = 0; i < n; ++i) {
        lua_pushlightuserdata(L, sds[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_pushinteger(L, static_cast<lua_Integer>(n));
    return 2;
}

int l_client_server_descriptions_destroy_all(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto n = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (n == 0) return 0;
    void** sds = new (std::nothrow) void*[n];
    if (!sds) return 0;
    for (size_t i = 0; i < n; ++i) {
        lua_rawgeti(L, 1, static_cast<int>(i + 1));
        sds[i] = lua_touserdata(L, -1);
        lua_pop(L, 1);
    }
    mongo::MongoClient::ServerDescriptionsDestroyAll(sds, n);
    delete[] sds;
    return 0;
}

int l_client_get_handshake_description(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    auto server_id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!client) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::MongoError error;
    void* desc = client->GetHandshakeDescription(server_id, opts, &error);
    if (!desc) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    lua_pushlightuserdata(L, desc);
    return 1;
}

int l_client_get_crypt_shared_version(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kMetaName);
    if (!client) { lua_pushnil(L); return 1; }
    const char* version = client->GetCryptSharedVersion();
    if (!version) { lua_pushnil(L); return 1; }
    lua_pushstring(L, version);
    return 1;
}

int l_client_set_usleep_impl(lua_State* L) {
    // Not directly usable from Lua — requires C function pointer
    return 0;
}

const luaL_Reg kLib[] = {
    {"client_new", l_client_new},
    {"client_new_from_uri", l_client_new_from_uri},
    {"client_new_from_uri_with_error", l_client_new_from_uri_with_error},
    {"client_destroy", l_client_destroy},
    {"client_set_appname", l_client_set_appname},
    {"client_set_socket_timeout_ms", l_client_set_socket_timeout_ms},
    {"client_set_ssl_opts", l_client_set_ssl_opts},
    {"client_set_error_api", l_client_set_error_api},
    {"client_reset", l_client_reset},
    {"client_release_from_pool", l_client_release_from_pool},
    {"client_from_pooled", l_client_from_pooled},
    {"client_get_database", l_client_get_database},
    {"client_get_default_database", l_client_get_default_database},
    {"client_get_collection", l_client_get_collection},
    {"client_command_simple", l_client_command_simple},
    {"client_command_simple_with_server_id", l_client_command_simple_with_server_id},
    {"client_command_with_opts", l_client_command_with_opts},
    {"client_read_command_with_opts", l_client_read_command_with_opts},
    {"client_write_command_with_opts", l_client_write_command_with_opts},
    {"client_read_write_command_with_opts", l_client_read_write_command_with_opts},
    {"client_start_session", l_client_start_session},
    {"client_set_read_prefs", l_client_set_read_prefs},
    {"client_get_read_prefs", l_client_get_read_prefs},
    {"client_set_write_concern", l_client_set_write_concern},
    {"client_get_write_concern", l_client_get_write_concern},
    {"client_set_read_concern", l_client_set_read_concern},
    {"client_get_read_concern", l_client_get_read_concern},
    {"client_get_uri", l_client_get_uri},
    {"client_set_server_api", l_client_set_server_api},
    {"client_watch", l_client_watch},
    {"client_get_database_names", l_client_get_database_names},
    {"client_get_database_names_with_opts", l_client_get_database_names_with_opts},
    {"client_find_databases_with_opts", l_client_find_databases_with_opts},
    {"client_set_apm_callbacks", l_client_set_apm_callbacks},
    {"client_set_structured_log_opts", l_client_set_structured_log_opts},
    {"client_set_oidc_callback", l_client_set_oidc_callback},
    {"client_enable_auto_encryption", l_client_enable_auto_encryption},
    {"client_append_metadata", l_client_append_metadata},
    {"client_get_gridfs", l_client_get_gridfs},
    {"client_select_server", l_client_select_server},
    {"client_get_server_description", l_client_get_server_description},
    {"client_get_server_descriptions", l_client_get_server_descriptions},
    {"client_server_descriptions_destroy_all", l_client_server_descriptions_destroy_all},
    {"client_get_handshake_description", l_client_get_handshake_description},
    {"client_get_crypt_shared_version", l_client_get_crypt_shared_version},
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
