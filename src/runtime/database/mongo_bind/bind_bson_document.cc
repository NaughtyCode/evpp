#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bson_document.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_oid.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.doc";

int l_gc(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    delete doc;
    *CheckUserdata<mongo::BsonDocument>(L, 1, kMetaName) = nullptr;
    return 0;
}

// ── Constructors ──────────────────────────────────────────────────────

int l_new(lua_State* L) {
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
    *ud = doc;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_from_json(lua_State* L) {
    size_t len;
    const char* json = luaL_checklstring(L, 1, &len);
    auto* doc = new (std::nothrow) mongo::BsonDocument(
        mongo::BsonDocument::NewFromJson(reinterpret_cast<const uint8_t*>(json), len));
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
    *ud = doc;
    return 1;
}

int l_from_data(lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    auto* doc = new (std::nothrow) mongo::BsonDocument(
        mongo::BsonDocument::NewFromData(reinterpret_cast<const uint8_t*>(data), len));
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, kMetaName);
    *ud = doc;
    return 1;
}

// ── Serialization ─────────────────────────────────────────────────────

int l_as_json(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    if (!doc) { lua_pushnil(L); return 1; }
    std::string json = doc->ToJson();
    lua_pushlstring(L, json.data(), json.size());
    return 1;
}

int l_get_data(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    if (!doc) { lua_pushnil(L); return 1; }
    const uint8_t* data = doc->GetData();
    uint32_t len = doc->GetLength();
    lua_pushlstring(L, reinterpret_cast<const char*>(data), len);
    return 1;
}

// ── Append fields ─────────────────────────────────────────────────────

int l_append_int32(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto val = luaL_checkinteger(L, 3);
    lua_pushboolean(L, doc && val <= INT32_MAX && val >= INT32_MIN
                        && doc->AppendInt32(key, static_cast<int32_t>(val)));
    return 1;
}

int l_append_int64(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto value = static_cast<int64_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, doc && doc->AppendInt64(key, value));
    return 1;
}

int l_append_double(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    double value = static_cast<double>(luaL_checknumber(L, 3));
    lua_pushboolean(L, doc && doc->AppendDouble(key, value));
    return 1;
}

int l_append_utf8(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    size_t len;
    const char* value = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, doc && doc->AppendUtf8(key, std::string_view(value, len)));
    return 1;
}

int l_append_bool(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    bool value = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, doc && doc->AppendBool(key, value));
    return 1;
}

int l_append_oid(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    const char* oid_str = luaL_checkstring(L, 3);
    mongo::MongoOid oid;
    if (!oid.IsValid(oid_str, strlen(oid_str))) {
        lua_pushboolean(L, false);
        return 1;
    }
    oid.InitFromString(oid_str);
    lua_pushboolean(L, doc && doc->AppendOid(key, oid));
    return 1;
}

int l_append_null(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, doc && doc->AppendNull(key));
    return 1;
}

int l_append_document(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 3, kMetaName);
    lua_pushboolean(L, doc && subdoc && doc->AppendDocument(key, *subdoc));
    return 1;
}

int l_append_timestamp(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto ts = luaL_checkinteger(L, 3);
    auto inc = luaL_checkinteger(L, 4);
    lua_pushboolean(L, doc && ts >= 0 && ts <= UINT32_MAX && inc >= 0 && inc <= UINT32_MAX
                        && doc->AppendTimestamp(key, static_cast<uint32_t>(ts), static_cast<uint32_t>(inc)));
    return 1;
}

int l_append_datetime(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto value = static_cast<int64_t>(luaL_checkinteger(L, 3));
    lua_pushboolean(L, doc && doc->AppendDateTime(key, value));
    return 1;
}

int l_append_binary(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    auto subtype = static_cast<int>(luaL_checkinteger(L, 3));
    size_t len;
    const char* data = luaL_checklstring(L, 4, &len);
    lua_pushboolean(L, doc && len <= UINT32_MAX
                        && doc->AppendBinary(key, subtype, reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len)));
    return 1;
}

// ── Query / access ────────────────────────────────────────────────────

int l_count_keys(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    lua_pushinteger(L, doc ? doc->CountKeys() : 0);
    return 1;
}

int l_has_field(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, doc && doc->HasField(key));
    return 1;
}

int l_empty(lua_State* L) {
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 1, kMetaName);
    lua_pushboolean(L, doc && doc->Empty());
    return 1;
}

const luaL_Reg kLib[] = {
    {"new", l_new},
    {"destroy", l_destroy},
    {"from_json", l_from_json},
    {"from_data", l_from_data},
    {"as_json", l_as_json},
    {"get_data", l_get_data},
    {"append_int32", l_append_int32},
    {"append_int64", l_append_int64},
    {"append_double", l_append_double},
    {"append_utf8", l_append_utf8},
    {"append_bool", l_append_bool},
    {"append_oid", l_append_oid},
    {"append_null", l_append_null},
    {"append_document", l_append_document},
    {"append_timestamp", l_append_timestamp},
    {"append_datetime", l_append_datetime},
    {"append_binary", l_append_binary},
    {"count_keys", l_count_keys},
    {"has_field", l_has_field},
    {"empty", l_empty},
    {nullptr, nullptr},
};

} // namespace

void RegisterBsonDocumentMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetBsonDocumentLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
