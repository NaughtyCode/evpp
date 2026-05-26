#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_exception.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_exception";

int l_bulk_write_exc_gc(lua_State* L) {
    auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
    delete exc;
    *CheckUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_bulk_write_exc_new(lua_State* L) {
    auto* exc = new (std::nothrow) mongo::MongoBulkWriteException();
    if (!exc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoBulkWriteException>(L, kMetaName);
    *ud = exc;
    return 1;
}

int l_bulk_write_exc_destroy(lua_State* L) { l_bulk_write_exc_gc(L); return 0; }

int l_bulk_write_exc_error(lua_State* L) {
    auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
    if (!exc) { lua_pushboolean(L, false); lua_pushnil(L); return 2; }
    mongo::MongoError error;
    bool ok = exc->Error(&error);
    lua_pushboolean(L, ok);
    if (ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

const luaL_Reg kLib[] = {
    {"bulk_write_exception_new", l_bulk_write_exc_new},
    {"bulk_write_exception_destroy", l_bulk_write_exc_destroy},
    {"bulk_write_exception_error", l_bulk_write_exc_error},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoBulkWriteExceptionMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_bulk_write_exc_gc);
}

const luaL_Reg* GetMongoBulkWriteExceptionLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
