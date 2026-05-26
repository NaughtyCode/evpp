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

int l_gc(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    if (cursor) cursor->Destroy();
    delete cursor;
    *CheckUserdata<mongo::MongoCursor>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_next(lua_State* L) {
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

int l_more(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushboolean(L, cursor && cursor->More());
    return 1;
}

int l_set_batch_size(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    auto val = luaL_checkinteger(L, 2);
    if (val < 0 || val > UINT32_MAX) return 0;
    if (cursor) cursor->SetBatchSize(static_cast<uint32_t>(val));
    return 0;
}

int l_set_limit(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    if (cursor) cursor->SetLimit(val);
    return 0;
}

int l_get_batch_size(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetBatchSize() : 0);
    return 1;
}

int l_get_server_id(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetServerId() : 0);
    return 1;
}

int l_get_id(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetId() : 0);
    return 1;
}

int l_get_limit(lua_State* L) {
    auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
    lua_pushinteger(L, cursor ? cursor->GetLimit() : 0);
    return 1;
}

const luaL_Reg kLib[] = {
    {"cursor_destroy", l_destroy},
    {"cursor_next", l_next},
    {"cursor_more", l_more},
    {"cursor_set_batch_size", l_set_batch_size},
    {"cursor_set_limit", l_set_limit},
    {"cursor_get_batch_size", l_get_batch_size},
    {"cursor_get_server_id", l_get_server_id},
    {"cursor_get_id", l_get_id},
    {"cursor_get_limit", l_get_limit},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoCursorMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoCursorLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
