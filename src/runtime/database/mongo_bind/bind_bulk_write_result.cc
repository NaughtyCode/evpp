#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_result.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bulkwrite.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_result";

int l_gc(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    delete result;
    *CheckUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* result = new (std::nothrow) mongo::MongoBulkWriteResult();
    if (!result) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoBulkWriteResult>(L, kMetaName);
    *ud = result;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_inserted_count(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->InsertedCount() : 0);
    return 1;
}

int l_upserted_count(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->UpsertedCount() : 0);
    return 1;
}

int l_matched_count(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->MatchedCount() : 0);
    return 1;
}

int l_modified_count(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->ModifiedCount() : 0);
    return 1;
}

int l_deleted_count(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->DeletedCount() : 0);
    return 1;
}

int l_server_id(lua_State* L) {
    auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
    lua_pushinteger(L, result ? result->ServerId() : 0);
    return 1;
}

const luaL_Reg kLib[] = {
    {"bulk_write_result_new", l_new},
    {"bulk_write_result_destroy", l_destroy},
    {"bulk_write_result_inserted_count", l_inserted_count},
    {"bulk_write_result_upserted_count", l_upserted_count},
    {"bulk_write_result_matched_count", l_matched_count},
    {"bulk_write_result_modified_count", l_modified_count},
    {"bulk_write_result_deleted_count", l_deleted_count},
    {"bulk_write_result_server_id", l_server_id},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoBulkWriteResultMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoBulkWriteResultLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
