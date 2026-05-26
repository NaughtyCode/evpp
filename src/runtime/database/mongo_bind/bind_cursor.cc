#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_cursor.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <cstdint>
#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.cursor";

int l_cursor_gc(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (cursor) cursor->Destroy();
    delete cursor;
    *CheckUserdata<mongo::MongoCursor>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_cursor_destroy(lua_State* L) { l_cursor_gc(L); return 0; }

int l_cursor_next(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (!cursor) { lua_pushboolean(L, false); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    bool ok = cursor->Next(doc);
    if (!ok) {
        delete *ud;
        *ud = nullptr;
        lua_pop(L, 1);
        mongo::MongoError error;
        if (cursor->HasError(&error)) {
            lua_pushnil(L);
            lua_pushstring(L, error.Message());
            return 2;
        }
        lua_pushboolean(L, false);
        return 1;
    }
    return 1;
}

int l_cursor_more(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushboolean(L, cursor && cursor->More());
    return 1;
}

int l_cursor_set_batch_size(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    auto val = luaL_checkinteger(L, 2);
    if (val < 0 || val > UINT32_MAX) return 0;
    if (cursor) cursor->SetBatchSize(static_cast<uint32_t>(val));
    return 0;
}

int l_cursor_set_limit(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    if (cursor) cursor->SetLimit(val);
    return 0;
}

int l_cursor_get_batch_size(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetBatchSize() : 0);
    return 1;
}

int l_cursor_get_server_id(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetServerId() : 0);
    return 1;
}

int l_cursor_get_id(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetId() : 0);
    return 1;
}

int l_cursor_get_limit(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetLimit() : 0);
    return 1;
}

int l_cursor_set_max_await_time_ms(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    auto val = luaL_checkinteger(L, 2);
    if (val < 0 || val > UINT32_MAX) return 0;
    if (cursor) cursor->SetMaxAwaitTimeMs(static_cast<uint32_t>(val));
    return 0;
}

int l_cursor_get_max_await_time_ms(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetMaxAwaitTimeMs() : 0);
    return 1;
}

int l_cursor_has_error(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (!cursor) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, cursor->HasError(&error));
    return 1;
}

int l_cursor_error_document(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (!cursor) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    const void* raw_doc = nullptr;
    bool has_err = cursor->ErrorDocument(&error, &raw_doc);
    lua_pushboolean(L, has_err);
    if (has_err) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_cursor_clone(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (!cursor) { lua_pushnil(L); return 1; }
    auto* clone = cursor->Clone();
    if (!clone) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoCursor>(L, kMetaName);
    *ud = clone;
    return 1;
}

const luaL_Reg kLib[] = {
    {"cursor_destroy", l_cursor_destroy},
    {"cursor_next", l_cursor_next},
    {"cursor_more", l_cursor_more},
    {"cursor_set_batch_size", l_cursor_set_batch_size},
    {"cursor_set_limit", l_cursor_set_limit},
    {"cursor_get_batch_size", l_cursor_get_batch_size},
    {"cursor_get_server_id", l_cursor_get_server_id},
    {"cursor_get_id", l_cursor_get_id},
    {"cursor_get_limit", l_cursor_get_limit},
    {"cursor_set_max_await_time_ms", l_cursor_set_max_await_time_ms},
    {"cursor_get_max_await_time_ms", l_cursor_get_max_await_time_ms},
    {"cursor_clone", l_cursor_clone},
    {"cursor_has_error", l_cursor_has_error},
    {"cursor_error_document", l_cursor_error_document},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoCursorMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_cursor_gc);
}

const luaL_Reg* GetMongoCursorLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
