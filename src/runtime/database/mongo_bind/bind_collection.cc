#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_collection.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.collection";

int l_gc(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (coll) coll->Destroy();
    delete coll;
    *CheckUserdata<mongo::MongoCollection>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_insert_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !doc) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->InsertOne(*doc, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_find(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !filter) { lua_pushnil(L); return 1; }
    auto* cursor = coll->FindWithOpts(*filter, opts, nullptr);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

int l_update_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* update = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
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

int l_update_many(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* update = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!coll || !selector || !update) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->UpdateMany(*selector, *update, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_delete_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
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

int l_delete_many(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !selector) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->DeleteMany(*selector, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_count(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !filter) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    int64_t count = coll->CountDocuments(*filter, opts, nullptr, &reply, &error);
    if (count < 0) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int l_drop(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (!coll) { lua_pushboolean(L, false); lua_pushnil(L); return 2; }
    mongo::MongoError error;
    bool ok = coll->Drop(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_get_name(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    const char* name = coll ? coll->GetName() : nullptr;
    if (name) lua_pushstring(L, name);
    else lua_pushnil(L);
    return 1;
}

int l_set_read_prefs(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (coll && prefs) coll->SetReadPrefs(*prefs);
    return 0;
}

int l_set_write_concern(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (coll && concern) coll->SetWriteConcern(*concern);
    return 0;
}

const luaL_Reg kLib[] = {
    {"coll_destroy", l_destroy},
    {"coll_insert_one", l_insert_one},
    {"coll_find", l_find},
    {"coll_update_one", l_update_one},
    {"coll_update_many", l_update_many},
    {"coll_delete_one", l_delete_one},
    {"coll_delete_many", l_delete_many},
    {"coll_count", l_count},
    {"coll_drop", l_drop},
    {"coll_get_name", l_get_name},
    {"coll_set_read_prefs", l_set_read_prefs},
    {"coll_set_write_concern", l_set_write_concern},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoCollectionMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoCollectionLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
