#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bson_iter.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <cstdint>
#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_oid.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.iter";

int l_gc(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    delete iter;
    *CheckUserdata<mongo::BsonIter>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, "bson.doc");
    if (!doc) { lua_pushnil(L); return 1; }
    auto* iter = new (std::nothrow) mongo::BsonIter(*doc);
    if (!iter) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonIter>(L, kMetaName);
    *ud = iter;
    return 1;
}

int l_next(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushboolean(L, iter && iter->Next());
    return 1;
}

int l_find(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, iter && iter->Find(key));
    return 1;
}

int l_key(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    const char* key = iter ? iter->Key() : nullptr;
    if (key) lua_pushstring(L, key);
    else lua_pushnil(L);
    return 1;
}

int l_type(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushinteger(L, iter ? iter->Type() : 0);
    return 1;
}

int l_as_int32(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushinteger(L, iter ? iter->AsInt32() : 0);
    return 1;
}

int l_as_int64(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushinteger(L, iter ? iter->AsInt64() : 0);
    return 1;
}

int l_as_double(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushnumber(L, iter ? iter->AsDouble() : 0.0);
    return 1;
}

int l_as_utf8(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    uint32_t len;
    const char* s = iter->AsUtf8(&len);
    if (s) lua_pushlstring(L, s, len);
    else lua_pushnil(L);
    return 1;
}

int l_as_bool(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushboolean(L, iter && iter->AsBool());
    return 1;
}

int l_as_datetime(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushinteger(L, iter ? iter->AsDateTime() : 0);
    return 1;
}

int l_as_oid(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    mongo::MongoOid oid = iter->AsOid();
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_recurse(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    auto* sub = new (std::nothrow) mongo::BsonIter(iter->Recurse());
    if (!sub) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonIter>(L, kMetaName);
    *ud = sub;
    return 1;
}

int l_as_binary(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    if (!iter) { lua_pushnil(L); return 1; }
    int subtype;
    uint32_t len;
    const uint8_t* data;
    iter->AsBinary(&subtype, &len, &data);
    lua_pushlstring(L, reinterpret_cast<const char*>(data), len);
    lua_pushinteger(L, subtype);
    return 2;
}

int l_offset(lua_State* L) {
    auto* iter = GetUserdata<mongo::BsonIter>(L, 1, kMetaName);
    lua_pushinteger(L, iter ? iter->Offset() : 0);
    return 1;
}

const luaL_Reg kLib[] = {
    {"iter_new", l_new},
    {"iter_next", l_next},
    {"iter_find", l_find},
    {"iter_key", l_key},
    {"iter_type", l_type},
    {"iter_as_int32", l_as_int32},
    {"iter_as_int64", l_as_int64},
    {"iter_as_double", l_as_double},
    {"iter_as_utf8", l_as_utf8},
    {"iter_as_bool", l_as_bool},
    {"iter_as_datetime", l_as_datetime},
    {"iter_as_oid", l_as_oid},
    {"iter_recurse", l_recurse},
    {"iter_as_binary", l_as_binary},
    {"iter_offset", l_offset},
    {nullptr, nullptr},
};

} // namespace

void RegisterBsonIterMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetBsonIterLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
