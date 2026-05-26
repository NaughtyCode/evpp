#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_operation.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulk.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk";

int l_gc(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    if (bulk) bulk->Destroy();
    *CheckUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    bool ordered = lua_toboolean(L, 1) != 0;
    auto* bulk = mongo::MongoBulkOperation::New(ordered);
    if (!bulk) { lua_pushnil(L); lua_pushstring(L, "failed to create bulk operation"); return 2; }
    auto** ud = NewUserdata<mongo::MongoBulkOperation>(L, kMetaName);
    *ud = bulk;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_insert(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (bulk && doc) bulk->Insert(*doc);
    return 0;
}

int l_remove_one(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (bulk && selector) bulk->RemoveOne(*selector);
    return 0;
}

int l_update_one(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* update = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    bool upsert = lua_toboolean(L, 4) != 0;
    if (bulk && selector && update) bulk->UpdateOne(*selector, *update, upsert);
    return 0;
}

int l_replace_one(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    bool upsert = lua_toboolean(L, 4) != 0;
    if (bulk && selector && doc) bulk->ReplaceOne(*selector, *doc, upsert);
    return 0;
}

int l_execute(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    if (!bulk) { lua_pushnil(L); lua_pushstring(L, "no bulk operation"); return 2; }
    mongo::BsonDocument reply;
    mongo::MongoError error;
    uint32_t server_id = bulk->Execute(&reply, &error);
    if (server_id == 0) {
        lua_pushnil(L);
        lua_pushstring(L, error.Message());
        return 2;
    }
    auto* doc = new (std::nothrow) mongo::BsonDocument(std::move(reply));
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    lua_pushinteger(L, server_id);
    return 2;
}

int l_set_write_concern(lua_State* L) {
    auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (bulk && concern) bulk->SetWriteConcern(*concern);
    return 0;
}

const luaL_Reg kLib[] = {
    {"bulk_new", l_new},
    {"bulk_destroy", l_destroy},
    {"bulk_insert", l_insert},
    {"bulk_remove_one", l_remove_one},
    {"bulk_update_one", l_update_one},
    {"bulk_replace_one", l_replace_one},
    {"bulk_execute", l_execute},
    {"bulk_set_write_concern", l_set_write_concern},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoBulkOperationMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoBulkOperationLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
