#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulkwrite";

int l_gc(lua_State* L) {
    auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
    if (bw) bw->Destroy();
    *CheckUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* bw = mongo::MongoBulkWrite::New();
    if (!bw) { lua_pushnil(L); lua_pushstring(L, "failed to create bulk write"); return 2; }
    auto** ud = NewUserdata<mongo::MongoBulkWrite>(L, kMetaName);
    *ud = bw;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_append_insert_one(lua_State* L) {
    auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
    const char* ns = luaL_checkstring(L, 2);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!bw || !doc) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::MongoError error;
    bool ok = bw->AppendInsertOne(ns, *doc, nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_append_update_one(lua_State* L) {
    auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
    const char* ns = luaL_checkstring(L, 2);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    auto* update = GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
    if (!bw || !filter || !update) {
        lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2;
    }
    mongo::MongoError error;
    bool ok = bw->AppendUpdateOne(ns, *filter, *update, nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_append_delete_one(lua_State* L) {
    auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
    const char* ns = luaL_checkstring(L, 2);
    auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    if (!bw || !filter) { lua_pushboolean(L, false); lua_pushstring(L, "invalid args"); return 2; }
    mongo::MongoError error;
    bool ok = bw->AppendDeleteOne(ns, *filter, nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_execute(lua_State* L) {
    auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
    if (!bw) { lua_pushnil(L); lua_pushstring(L, "invalid bulk write"); return 2; }

    auto ret = bw->Execute(nullptr);

    if (ret.exception) {
        mongo::MongoError error;
        ret.exception->Error(&error);
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        delete ret.exception;
        delete ret.result;
        return 2;
    }

    auto* result_doc = new (std::nothrow) mongo::BsonDocument();
    if (!result_doc) {
        lua_pushnil(L); lua_pushstring(L, "allocation failure");
        delete ret.result; return 2;
    }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = result_doc;
    lua_pushinteger(L, ret.result ? ret.result->InsertedCount() : 0);
    lua_pushinteger(L, ret.result ? ret.result->DeletedCount() : 0);
    lua_pushinteger(L, ret.result ? ret.result->MatchedCount() : 0);
    lua_pushinteger(L, ret.result ? ret.result->ModifiedCount() : 0);
    lua_pushinteger(L, ret.result ? ret.result->UpsertedCount() : 0);
    delete ret.result;
    return 6;
}

const luaL_Reg kLib[] = {
    {"bulkwrite_new", l_new},
    {"bulkwrite_destroy", l_destroy},
    {"bulkwrite_append_insert_one", l_append_insert_one},
    {"bulkwrite_append_update_one", l_append_update_one},
    {"bulkwrite_append_delete_one", l_append_delete_one},
    {"bulkwrite_execute", l_execute},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoBulkWriteMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoBulkWriteLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
