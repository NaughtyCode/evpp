#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_database.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.database";

int l_gc(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    if (db) db->Destroy();
    delete db;
    *CheckUserdata<mongo::MongoDatabase>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_get_collection(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    const char* name = luaL_checkstring(L, 2);
    if (!db) { lua_pushnil(L); return 1; }
    auto* coll = db->GetCollection(name);
    if (!coll) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCollection>(L, "mongoc.collection");
    *ud = coll;
    return 1;
}

int l_get_name(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    const char* name = db ? db->GetName() : nullptr;
    if (name) lua_pushstring(L, name);
    else lua_pushnil(L);
    return 1;
}

int l_drop(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    if (!db) { lua_pushboolean(L, false); lua_pushnil(L); return 2; }
    mongo::MongoError error;
    bool ok = db->Drop(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_command_simple(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!db || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = db->CommandSimple(*cmd, nullptr, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) {
        lua_pushstring(L, error.Message());
        lua_pushnil(L);
    } else {
        auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
        if (!doc) { lua_pushnil(L); lua_pushnil(L); return 3; }
        lua_pushnil(L);
        auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
        *ud = doc;
    }
    return 3;
}

int l_set_read_prefs(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (db && prefs) db->SetReadPrefs(*prefs);
    return 0;
}

int l_set_write_concern(lua_State* L) {
    auto* db = GetUserdata<mongo::MongoDatabase>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (db && concern) db->SetWriteConcern(*concern);
    return 0;
}

const luaL_Reg kLib[] = {
    {"db_destroy", l_destroy},
    {"db_get_collection", l_get_collection},
    {"db_get_name", l_get_name},
    {"db_drop", l_drop},
    {"db_command_simple", l_command_simple},
    {"db_set_read_prefs", l_set_read_prefs},
    {"db_set_write_concern", l_set_write_concern},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoDatabaseMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoDatabaseLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
