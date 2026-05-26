#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_collection.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>
#include <vector>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulk.h"
#include "runtime/database/mongo/mongo_change_stream.h"
#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_find_and_modify_opts.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.collection";

int l_coll_gc(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (coll) coll->Destroy();
    delete coll;
    *CheckUserdata<mongo::MongoCollection>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_coll_destroy(lua_State* L) { l_coll_gc(L); return 0; }

int l_coll_insert_one(lua_State* L) {
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

int l_coll_find(lua_State* L) {
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

int l_coll_update_one(lua_State* L) {
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

int l_coll_update_many(lua_State* L) {
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

int l_coll_delete_one(lua_State* L) {
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

int l_coll_delete_many(lua_State* L) {
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

int l_coll_count(lua_State* L) {
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

int l_coll_drop(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (!coll) { lua_pushboolean(L, false); lua_pushnil(L); return 2; }
    mongo::MongoError error;
    bool ok = coll->Drop(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_copy(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (!coll) { lua_pushnil(L); return 1; }
    auto* copy = coll->Copy();
    if (!copy) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCollection>(L, kMetaName);
    *ud = copy;
    return 1;
}

int l_coll_get_name(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    const char* name = coll ? coll->GetName() : nullptr;
    if (name) lua_pushstring(L, name);
    else lua_pushnil(L);
    return 1;
}

int l_coll_set_read_prefs(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (coll && prefs) coll->SetReadPrefs(*prefs);
    return 0;
}

int l_coll_set_write_concern(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (coll && concern) coll->SetWriteConcern(*concern);
    return 0;
}

int l_coll_set_read_concern(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
    if (coll && concern) coll->SetReadConcern(*concern);
    return 0;
}

int l_coll_watch(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* pipeline = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !pipeline) { lua_pushnil(L); return 1; }
    auto* stream = coll->Watch(*pipeline, opts);
    if (!stream) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoChangeStream>(L, "mongoc.change_stream");
    *ud = stream;
    return 1;
}

int l_coll_find_and_modify(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* query = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                  : GetUserdata<mongo::MongoFindAndModifyOpts>(L, 3, "mongoc.find_and_modify_opts");
    if (!coll || !query) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->FindAndModify(*query, opts, &reply, &error);
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

int l_coll_create_index(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* keys = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !keys) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->CreateIndex(*keys, opts, &reply, &error);
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

int l_coll_drop_index(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    const char* name = luaL_checkstring(L, 2);
    if (!coll) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, coll->DropIndex(name, &error));
    return 1;
}

int l_coll_find_indexes(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!coll) { lua_pushnil(L); return 1; }
    auto* cursor = coll->FindIndexes(opts);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

int l_coll_create_bulk_operation(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    bool ordered = lua_toboolean(L, 2) != 0;
    if (!coll) { lua_pushnil(L); return 1; }
    auto* bulk = coll->CreateBulkOperation(ordered, nullptr);
    if (!bulk) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoBulkOperation>(L, "mongoc.bulk_operation");
    *ud = bulk;
    return 1;
}

int l_coll_create_bulk_operation_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* opts = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!coll || !opts) { lua_pushnil(L); return 1; }
    auto* bulk = coll->CreateBulkOperationWithOpts(opts);
    if (!bulk) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoBulkOperation>(L, "mongoc.bulk_operation");
    *ud = bulk;
    return 1;
}

int l_coll_drop_index_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    const char* name = luaL_checkstring(L, 2);
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, coll->DropIndexWithOpts(name, opts, &error));
    return 1;
}

int l_coll_estimated_document_count(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    if (!coll) { lua_pushnil(L); return 1; }
    mongo::MongoError error;
    int64_t count = coll->EstimatedDocumentCount(opts, prefs, &error);
    if (count < 0) { lua_pushnil(L); lua_pushstring(L, error.Message()); return 2; }
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int l_coll_rename(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    const char* new_db = luaL_checkstring(L, 2);
    const char* new_name = luaL_checkstring(L, 3);
    bool drop_target = lua_toboolean(L, 4) != 0;
    if (!coll) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, coll->Rename(new_db, new_name, drop_target, &error));
    return 1;
}

int l_coll_replace_one(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* replacement = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!coll || !selector || !replacement) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->ReplaceOne(*selector, *replacement, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_insert_many(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    if (!coll || !lua_istable(L, 2)) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }

    int n = static_cast<int>(lua_rawlen(L, 2));
    if (n <= 0) { lua_pushboolean(L, false); lua_pushstring(L, "empty table"); return 2; }

    std::vector<const mongo::BsonDocument*> docs(n);
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        docs[i-1] = GetUserdata<mongo::BsonDocument>(L, -1, "bson.doc");
        lua_pop(L, 1);
        if (!docs[i-1]) { lua_pushboolean(L, false); lua_pushstring(L, "invalid doc in table"); return 2; }
    }

    auto* opts = lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->InsertMany(docs.data(), n, opts, &reply, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_aggregate(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* pipeline = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !pipeline) { lua_pushnil(L); return 1; }
    auto* cursor = coll->Aggregate(*pipeline, opts, nullptr);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, "mongoc.cursor");
    *ud = cursor;
    return 1;
}

int l_coll_command_simple(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    if (!coll || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->CommandSimple(*cmd, prefs, &reply, &error);
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

int l_coll_command_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!coll || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->CommandWithOpts(*cmd, prefs, opts, &reply, &error);
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

int l_coll_read_command_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!coll || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->ReadCommandWithOpts(*cmd, prefs, opts, &reply, &error);
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

int l_coll_write_command_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* opts = lua_isnoneornil(L, 3) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!coll || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->WriteCommandWithOpts(*cmd, opts, &reply, &error);
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

int l_coll_read_write_command_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* cmd = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* prefs = lua_isnoneornil(L, 3) ? nullptr
                   : GetUserdata<mongo::MongoReadPrefs>(L, 3, "mongoc.read_prefs");
    auto* opts = lua_isnoneornil(L, 4) ? nullptr
                  : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!coll || !cmd) { lua_pushnil(L); lua_pushstring(L, "invalid args"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    bool ok = coll->ReadWriteCommandWithOpts(*cmd, prefs, opts, &reply, &error);
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

int l_coll_drop_with_opts(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* opts = lua_isnoneornil(L, 2) ? nullptr
                 : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!coll) { lua_pushboolean(L, false); lua_pushnil(L); return 2; }
    mongo::MongoError error;
    bool ok = coll->DropWithOpts(opts, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_coll_keys_to_index_string(lua_State* L) {
    auto* coll = GetUserdata<mongo::MongoCollection>(L, 1, kMetaName);
    auto* keys = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!coll || !keys) { lua_pushnil(L); return 1; }
    char* str = coll->KeysToIndexString(*keys);
    if (str) { lua_pushstring(L, str); bson_free(str); }
    else lua_pushnil(L);
    return 1;
}

const luaL_Reg kLib[] = {
    {"coll_destroy", l_coll_destroy},
    {"coll_insert_one", l_coll_insert_one},
    {"coll_find", l_coll_find},
    {"coll_update_one", l_coll_update_one},
    {"coll_update_many", l_coll_update_many},
    {"coll_replace_one", l_coll_replace_one},
    {"coll_delete_one", l_coll_delete_one},
    {"coll_delete_many", l_coll_delete_many},
    {"coll_insert_many", l_coll_insert_many},
    {"coll_aggregate", l_coll_aggregate},
    {"coll_count", l_coll_count},
    {"coll_drop", l_coll_drop},
    {"coll_copy", l_coll_copy},
    {"coll_get_name", l_coll_get_name},
    {"coll_set_read_prefs", l_coll_set_read_prefs},
    {"coll_set_write_concern", l_coll_set_write_concern},
    {"coll_set_read_concern", l_coll_set_read_concern},
    {"coll_watch", l_coll_watch},
    {"coll_find_and_modify", l_coll_find_and_modify},
    {"coll_create_index", l_coll_create_index},
    {"coll_drop_index", l_coll_drop_index},
    {"coll_find_indexes", l_coll_find_indexes},
    {"coll_create_bulk_operation", l_coll_create_bulk_operation},
    {"coll_create_bulk_operation_with_opts", l_coll_create_bulk_operation_with_opts},
    {"coll_drop_index_with_opts", l_coll_drop_index_with_opts},
    {"coll_estimated_document_count", l_coll_estimated_document_count},
    {"coll_rename", l_coll_rename},
    {"coll_command_simple", l_coll_command_simple},
    {"coll_command_with_opts", l_coll_command_with_opts},
    {"coll_read_command_with_opts", l_coll_read_command_with_opts},
    {"coll_write_command_with_opts", l_coll_write_command_with_opts},
    {"coll_read_write_command_with_opts", l_coll_read_write_command_with_opts},
    {"coll_drop_with_opts", l_coll_drop_with_opts},
    {"coll_keys_to_index_string", l_coll_keys_to_index_string},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoCollectionMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_coll_gc);
}

const luaL_Reg* GetMongoCollectionLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
