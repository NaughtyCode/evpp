#include "runtime/database/mongo_bind/mongo_bind.h"

#include <cstdint>
#include <cstring>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "runtime/core/log/log.h"
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_oid.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo/mongo_system.h"
#include "runtime/database/mongo/mongo_uri.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace script {
namespace {

// ═══════════════════════════════════════════════════════════════════════
// Userdata helpers — each type gets a metatable with __gc.
// The userdata stores a single T* pointer.
// ═══════════════════════════════════════════════════════════════════════

template <typename T>
T** CheckUserdata(lua_State* L, int idx, const char* meta_name) {
    return static_cast<T**>(luaL_checkudata(L, idx, meta_name));
}

template <typename T>
T* GetUserdata(lua_State* L, int idx, const char* meta_name) {
    return *CheckUserdata<T>(L, idx, meta_name);
}

template <typename T>
T** NewUserdata(lua_State* L, const char* meta_name) {
    auto* ud = static_cast<T**>(lua_newuserdata(L, sizeof(T*)));
    *ud = nullptr;
    luaL_setmetatable(L, meta_name);
    return ud;
}

// ═══════════════════════════════════════════════════════════════════════
// BSON metatable ("bson.doc")
// ═══════════════════════════════════════════════════════════════════════

const char* kBsonMetaName = "bson.doc";

int l_bson_gc(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    delete doc;
    *CheckUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName) = nullptr;
    return 0;
}

int l_bson_new(lua_State* L) {
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kBsonMetaName);
    *ud = new mongo::BsonDocument();
    return 1;
}

int l_bson_destroy(lua_State* L) {
    l_bson_gc(L);
    return 0;
}

int l_bson_from_json(lua_State* L) {
    size_t len;
    const char* json = luaL_checklstring(L, 1, &len);
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kBsonMetaName);
    *ud = new mongo::BsonDocument(
        mongo::BsonDocument::NewFromJson(reinterpret_cast<const uint8_t*>(json), len));
    return 1;
}

int l_bson_as_json(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    if (!doc) { lua_pushnil(L); return 1; }
    std::string json = doc->ToJson();
    lua_pushlstring(L, json.data(), json.size());
    return 1;
}

int l_bson_append_int32(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto value = static_cast<int32_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, doc && doc->AppendInt32(key, value));
    return 1;
}

int l_bson_append_int64(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto value = static_cast<int64_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, doc && doc->AppendInt64(key, value));
    return 1;
}

int l_bson_append_double(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    double value = static_cast<double>(luaL_checknumber(L, 3));
    lua_pushboolean(L, doc && doc->AppendDouble(key, value));
    return 1;
}

int l_bson_append_utf8(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    size_t len;
    const char* value = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, doc && doc->AppendUtf8(key, std::string_view(value, len)));
    return 1;
}

int l_bson_append_bool(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    bool value = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, doc && doc->AppendBool(key, value));
    return 1;
}

int l_bson_append_oid(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    const char* oid_str = luaL_checkstring(L, 3);
    mongo::MongoOid oid;
    oid.InitFromString(oid_str);
    lua_pushboolean(L, doc && doc->AppendOid(key, oid));
    return 1;
}

int l_bson_append_null(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, doc && doc->AppendNull(key));
    return 1;
}

int l_bson_append_document(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    lua_pushboolean(L, doc && subdoc && doc->AppendDocument(key, *subdoc));
    return 1;
}

int l_bson_append_timestamp(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto timestamp = static_cast<uint32_t>(luaL_checkinteger(L, 3));
    auto increment = static_cast<uint32_t>(luaL_checkinteger(L, 4));
    lua_pushboolean(L, doc && doc->AppendTimestamp(key, timestamp, increment));
    return 1;
}

int l_bson_count_keys(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    lua_pushinteger(L, doc ? doc->CountKeys() : 0);
    return 1;
}

int l_bson_has_field(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, doc && doc->HasField(key));
    return 1;
}

// ── Iteration ────────────────────────────────────────────────────────

const char* kBsonIterMetaName = "bson.iter";

int l_bson_iter_gc(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    delete iter;
    *CheckUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName) = nullptr;
    return 0;
}

int l_bson_iter_new(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kBsonMetaName);
    if (!doc) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::BsonIter>(L, kBsonIterMetaName);
    *ud = new mongo::BsonIter(*doc);
    return 1;
}

int l_bson_iter_next(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushboolean(L, iter && iter->Next());
    return 1;
}

int l_bson_iter_key(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    const char* key = iter ? iter->Key() : nullptr;
    if (key) lua_pushstring(L, key);
    else lua_pushnil(L);
    return 1;
}

int l_bson_iter_type(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushinteger(L, iter ? iter->Type() : 0);
    return 1;
}

int l_bson_iter_as_int32(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushinteger(L, iter ? iter->AsInt32() : 0);
    return 1;
}

int l_bson_iter_as_int64(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushinteger(L, iter ? iter->AsInt64() : 0);
    return 1;
}

int l_bson_iter_as_double(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushnumber(L, iter ? iter->AsDouble() : 0.0);
    return 1;
}

int l_bson_iter_as_utf8(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    uint32_t len;
    const char* s = iter->AsUtf8(&len);
    if (s) lua_pushlstring(L, s, len);
    else lua_pushnil(L);
    return 1;
}

int l_bson_iter_as_bool(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushboolean(L, iter && iter->AsBool());
    return 1;
}

int l_bson_iter_as_datetime(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    lua_pushinteger(L, iter ? iter->AsDateTime() : 0);
    return 1;
}

int l_bson_iter_recurse(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kBsonIterMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::BsonIter>(L, kBsonIterMetaName);
    *ud = new mongo::BsonIter(iter->Recurse());
    return 1;
}

// ── OID helpers (returned as strings) ────────────────────────────────

int l_bson_oid_new(lua_State* L) {
    mongo::MongoOid oid;
    oid.Init();
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_bson_oid_is_valid(lua_State* L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    mongo::MongoOid oid;
    lua_pushboolean(L, oid.IsValid(str, len));
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════
// Client metatable ("mongoc.client")
// ═══════════════════════════════════════════════════════════════════════

const char* kClientMetaName = "mongoc.client";

int l_client_gc(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kClientMetaName);
    if (client) client->Destroy();
    delete client;
    *CheckUserdata<mongo::MongoClient>(L, 1, kClientMetaName) = nullptr;
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
    auto** ud = NewUserdata<mongo::MongoClient>(L, kClientMetaName);
    *ud = client;
    return 1;
}

int l_client_destroy(lua_State* L) {
    l_client_gc(L);
    return 0;
}

int l_client_set_appname(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kClientMetaName);
    const char* appname = luaL_checkstring(L, 2);
    if (client) client->SetAppname(appname);
    return 0;
}

int l_client_get_database(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kClientMetaName);
    const char* name = luaL_checkstring(L, 2);
    if (!client) { lua_pushnil(L); return 1; }
    auto* db = client->GetDatabase(name);
    if (!db) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoDatabase>(L, "mongoc.database");
    *ud = db;
    return 1;
}

int l_client_get_collection(lua_State* L) {
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kClientMetaName);
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
    auto* client = GetUserdata<mongo::MongoClient>(L, 1, kClientMetaName);
    const char* db_name = luaL_checkstring(L, 2);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    if (!client || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = client->CommandSimple(db_name, *cmd, nullptr, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) {
        lua_pushstring(L, error.Message());
        lua_pushnil(L); // no reply on error
    } else {
        lua_pushnil(L); // no error
        auto** ud = NewUserdata<mongo::BsonDocument>(L, kBsonMetaName);
        *ud = new mongo::BsonDocument(std::move(reply));
    }
    return 3; // ok, err, reply
}

// ═══════════════════════════════════════════════════════════════════════
// Database metatable ("mongoc.database")
// ═══════════════════════════════════════════════════════════════════════

const char* kDatabaseMetaName = "mongoc.database";

int l_db_gc(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kDatabaseMetaName);
    if (db) db->Destroy();
    delete db;
    *CheckUserdata<mongo::MongoDatabase>(L, 1, kDatabaseMetaName) = nullptr;
    return 0;
}

int l_db_destroy(lua_State* L) { l_db_gc(L); return 0; }

int l_db_get_collection(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kDatabaseMetaName);
    const char* name = luaL_checkstring(L, 2);
    if (!db) { lua_pushnil(L); return 1; }
    auto* coll = db->GetCollection(name);
    if (!coll) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCollection>(L, "mongoc.collection");
    *ud = coll;
    return 1;
}

int l_db_get_name(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kDatabaseMetaName);
    const char* name = db ? db->GetName() : nullptr;
    if (name) lua_pushstring(L, name);
    else lua_pushnil(L);
    return 1;
}

int l_db_drop(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kDatabaseMetaName);
    if (!db) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    bool ok = db->Drop(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

// ═══════════════════════════════════════════════════════════════════════
// Collection metatable ("mongoc.collection")
// ═══════════════════════════════════════════════════════════════════════

const char* kCollectionMetaName = "mongoc.collection";

int l_coll_gc(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    if (coll) coll->Destroy();
    delete coll;
    *CheckUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName) = nullptr;
    return 0;
}

int l_coll_destroy(lua_State* L) { l_coll_gc(L); return 0; }

int l_coll_insert_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, kBsonMetaName);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                : GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    if (!coll || !doc) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->InsertOne(*doc, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_find(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, kBsonMetaName);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    if (!coll || !filter) { lua_pushnil(L); return 1; }
    auto* cursor = coll->FindWithOpts(*filter, opts, nullptr);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

int l_coll_update_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, kBsonMetaName);
    auto* update = GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 4, kBsonMetaName);
    if (!coll || !selector || !update) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->UpdateOne(*selector, *update, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_delete_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, kBsonMetaName);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    if (!coll || !selector) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->DeleteOne(*selector, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_count(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, kBsonMetaName);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, kBsonMetaName);
    if (!coll || !filter) { lua_pushinteger(L, -1); return 1; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    int64_t count = coll->CountDocuments(*filter, opts, nullptr, &reply, &error);
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int l_coll_drop(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    if (!coll) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    bool ok = coll->Drop(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_get_name(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kCollectionMetaName);
    const char* name = coll ? coll->GetName() : nullptr;
    if (name) lua_pushstring(L, name);
    else lua_pushnil(L);
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════
// Cursor metatable ("mongoc.cursor")
// ═══════════════════════════════════════════════════════════════════════

const char* kCursorMetaName = "mongoc.cursor";

int l_cursor_gc(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kCursorMetaName);
    if (cursor) cursor->Destroy();
    delete cursor;
    *CheckUserdata<mongo::MongoCursor>(L, 1, kCursorMetaName) = nullptr;
    return 0;
}

int l_cursor_destroy(lua_State* L) { l_cursor_gc(L); return 0; }

int l_cursor_next(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kCursorMetaName);
    if (!cursor) { lua_pushboolean(L, false); return 1; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kBsonMetaName);
    *ud = new mongo::BsonDocument();
    bool ok = cursor->Next(*ud);
    if (!ok) {
        delete *ud;
        *ud = nullptr;
        lua_pop(L, 1);
        // Check for error
        mongo::MongoError error;
        if (cursor->HasError(&error)) {
            lua_pushnil(L);
            lua_pushstring(L, error.Message());
            return 2;
        }
        lua_pushboolean(L, false);
        return 1;
    }
    return 1; // returns the bson doc userdata
}

int l_cursor_set_batch_size(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kCursorMetaName);
    auto batch_size = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (cursor) cursor->SetBatchSize(batch_size);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// Registration tables
// ═══════════════════════════════════════════════════════════════════════

const luaL_Reg kBsonFunctions[] = {
    {"new", l_bson_new},
    {"destroy", l_bson_destroy},
    {"from_json", l_bson_from_json},
    {"as_json", l_bson_as_json},
    {"append_int32", l_bson_append_int32},
    {"append_int64", l_bson_append_int64},
    {"append_double", l_bson_append_double},
    {"append_utf8", l_bson_append_utf8},
    {"append_bool", l_bson_append_bool},
    {"append_oid", l_bson_append_oid},
    {"append_null", l_bson_append_null},
    {"append_document", l_bson_append_document},
    {"append_timestamp", l_bson_append_timestamp},
    {"count_keys", l_bson_count_keys},
    {"has_field", l_bson_has_field},
    // Iteration
    {"iter_new", l_bson_iter_new},
    {"iter_next", l_bson_iter_next},
    {"iter_key", l_bson_iter_key},
    {"iter_type", l_bson_iter_type},
    {"iter_as_int32", l_bson_iter_as_int32},
    {"iter_as_int64", l_bson_iter_as_int64},
    {"iter_as_double", l_bson_iter_as_double},
    {"iter_as_utf8", l_bson_iter_as_utf8},
    {"iter_as_bool", l_bson_iter_as_bool},
    {"iter_as_datetime", l_bson_iter_as_datetime},
    {"iter_recurse", l_bson_iter_recurse},
    // OID
    {"oid_new", l_bson_oid_new},
    {"oid_is_valid", l_bson_oid_is_valid},
    {nullptr, nullptr},
};

const luaL_Reg kMongocFunctions[] = {
    {"client_new", l_client_new},
    {"client_destroy", l_client_destroy},
    {"client_set_appname", l_client_set_appname},
    {"client_get_database", l_client_get_database},
    {"client_get_collection", l_client_get_collection},
    {"client_command_simple", l_client_command_simple},
    {"db_destroy", l_db_destroy},
    {"db_get_collection", l_db_get_collection},
    {"db_get_name", l_db_get_name},
    {"db_drop", l_db_drop},
    {"coll_destroy", l_coll_destroy},
    {"coll_insert_one", l_coll_insert_one},
    {"coll_find", l_coll_find},
    {"coll_update_one", l_coll_update_one},
    {"coll_delete_one", l_coll_delete_one},
    {"coll_count", l_coll_count},
    {"coll_drop", l_coll_drop},
    {"coll_get_name", l_coll_get_name},
    {"cursor_destroy", l_cursor_destroy},
    {"cursor_next", l_cursor_next},
    {"cursor_set_batch_size", l_cursor_set_batch_size},
    {nullptr, nullptr},
};

// ── Metatable registration (called once before pushing libraries) ────

void RegisterMetatable(lua_State* L, const char* name, const luaL_Reg* funcs,
                       lua_CFunction gc_func) {
    luaL_newmetatable(L, name);
    if (funcs) {
        luaL_setfuncs(L, funcs, 0);
    }
    lua_pushcfunction(L, gc_func);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
}

void RegisterAllMetatables(lua_State* L) {
    RegisterMetatable(L, kBsonMetaName, nullptr, l_bson_gc);
    RegisterMetatable(L, kBsonIterMetaName, nullptr, l_bson_iter_gc);
    RegisterMetatable(L, kClientMetaName, nullptr, l_client_gc);
    RegisterMetatable(L, kDatabaseMetaName, nullptr, l_db_gc);
    RegisterMetatable(L, kCollectionMetaName, nullptr, l_coll_gc);
    RegisterMetatable(L, kCursorMetaName, nullptr, l_cursor_gc);
}

} // namespace

void ExportMongo(ScriptVM& vm) {
    lua_State* L = vm.GetState();
    if (!L) return;

    RegisterAllMetatables(L);

    // Register "bson" global module
    vm.RegisterModule("bson", kBsonFunctions);

    // Register "mongoc" global module
    vm.RegisterModule("mongoc", kMongocFunctions);

    ENGINE_LOG_INFO(GetLogger(), "[mongo] Lua bindings registered");
}

} // namespace script
} // namespace engine
