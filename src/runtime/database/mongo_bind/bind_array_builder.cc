#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_array_builder.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <bson/bson.h>
#include <cstdint>
#include <cstring>
#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_oid.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.array_builder";

int l_array_builder_gc(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    delete builder;
    *CheckUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_array_builder_new(lua_State* L) {
    auto* builder = new (std::nothrow) mongo::BsonArrayBuilder();
    if (!builder) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonArrayBuilder>(L, kMetaName);
    *ud = builder;
    return 1;
}

int l_array_builder_destroy(lua_State* L) { l_array_builder_gc(L); return 0; }

int l_array_builder_build(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    if (!builder) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument();
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    if (!builder->Build(doc)) {
        delete doc;
        lua_pushnil(L);
        return 1;
    }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_array_builder_append_int32(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int32_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendInt32(val));
    return 1;
}

int l_array_builder_append_int64(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendInt64(val));
    return 1;
}

int l_array_builder_append_double(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    double val = static_cast<double>(luaL_checknumber(L, 2));
    lua_pushboolean(L, builder && builder->AppendDouble(val));
    return 1;
}

int l_array_builder_append_utf8(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendUtf8(val));
    return 1;
}

int l_array_builder_append_bool(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    bool val = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, builder && builder->AppendBool(val));
    return 1;
}

int l_array_builder_append_null(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendNull());
    return 1;
}

int l_array_builder_append_document(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && doc && builder->AppendDocument(*doc));
    return 1;
}

int l_array_builder_append_array(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && doc && builder->AppendArray(*doc));
    return 1;
}

int l_array_builder_append_datetime(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendDateTime(val));
    return 1;
}

int l_array_builder_append_binary(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto subtype = static_cast<int>(luaL_checkinteger(L, 2));
    size_t len;
    const char* data = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, builder && len <= UINT32_MAX
                        && builder->AppendBinary(subtype, reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len)));
    return 1;
}

int l_array_builder_append_binary_uninit(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto subtype = static_cast<int>(luaL_checkinteger(L, 2));
    auto len = static_cast<uint32_t>(luaL_checkinteger(L, 3));
    uint8_t* data_out = nullptr;
    bool ok = builder && builder->AppendBinaryUninit(subtype, &data_out, len);
    lua_pushboolean(L, ok);
    if (ok && data_out) lua_pushlightuserdata(L, data_out);
    else lua_pushnil(L);
    return 2;
}

int l_array_builder_append_regex(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* regex = luaL_checkstring(L, 2);
    const char* options = luaL_checkstring(L, 3);
    lua_pushboolean(L, builder && builder->AppendRegex(regex, options));
    return 1;
}

int l_array_builder_append_code(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* code = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendCode(code));
    return 1;
}

int l_array_builder_append_minkey(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendMinkey());
    return 1;
}

int l_array_builder_append_maxkey(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendMaxkey());
    return 1;
}

int l_array_builder_append_undefined(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendUndefined());
    return 1;
}

int l_array_builder_append_symbol(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendSymbol(val));
    return 1;
}

int l_array_builder_append_oid(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* oid_str = luaL_checkstring(L, 2);
    mongo::MongoOid oid;
    if (!oid.IsValid(oid_str, strlen(oid_str))) {
        lua_pushboolean(L, false);
        return 1;
    }
    oid.InitFromString(oid_str);
    lua_pushboolean(L, builder && builder->AppendOid(oid));
    return 1;
}

int l_array_builder_append_timestamp(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto ts = luaL_checkinteger(L, 2);
    auto inc = luaL_checkinteger(L, 3);
    lua_pushboolean(L, builder && ts >= 0 && ts <= UINT32_MAX && inc >= 0 && inc <= UINT32_MAX
                        && builder->AppendTimestamp(static_cast<uint32_t>(ts), static_cast<uint32_t>(inc)));
    return 1;
}

int l_array_builder_append_now_utc(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendNowUtc());
    return 1;
}

int l_array_builder_append_decimal128(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* dec_str = luaL_checkstring(L, 2);
    mongo::MongoDecimal128 dec;
    if (!dec.FromString(dec_str)) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, builder && builder->AppendDecimal128(dec));
    return 1;
}

int l_array_builder_append_code_with_scope(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* javascript = luaL_checkstring(L, 2);
    auto* scope = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
    lua_pushboolean(L, builder && scope && builder->AppendCodeWithScope(javascript, *scope));
    return 1;
}

int l_array_builder_append_iter(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
    lua_pushboolean(L, builder && iter && builder->AppendIter(*iter));
    return 1;
}

int l_array_builder_append_db_pointer(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* collection = luaL_checkstring(L, 2);
    const char* oid_str = luaL_checkstring(L, 3);
    mongo::MongoOid oid;
    if (!oid.IsValid(oid_str, strlen(oid_str))) {
        lua_pushboolean(L, false);
        return 1;
    }
    oid.InitFromString(oid_str);
    lua_pushboolean(L, builder && builder->AppendDBPointer(collection, oid));
    return 1;
}

int l_array_builder_append_time_t(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    time_t value = static_cast<time_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendTimeT(value));
    return 1;
}

int l_array_builder_append_timeval(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto tv_sec = static_cast<long>(luaL_checkinteger(L, 2));
    auto tv_usec = static_cast<long>(luaL_checkinteger(L, 3));
    struct timeval tv;
    tv.tv_sec = tv_sec;
    tv.tv_usec = tv_usec;
    lua_pushboolean(L, builder && builder->AppendTimeval(&tv));
    return 1;
}

int l_array_builder_append_document_begin(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && subdoc && builder->AppendDocumentBegin(subdoc));
    return 1;
}

int l_array_builder_append_document_end(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* subdoc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && subdoc &&
                    mongo::BsonArrayBuilder::AppendDocumentEnd(builder, subdoc));
    return 1;
}

int l_array_builder_append_value(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    void* bson_value = lua_touserdata(L, 2);
    lua_pushboolean(L, builder && bson_value && builder->AppendValue(bson_value));
    return 1;
}

int l_array_builder_append_array_from_vector(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* iter = GetUserdata<mongo::BsonIter>(L, 2, "bson.iter");
    lua_pushboolean(L, builder && iter && builder->AppendArrayFromVector(*iter));
    return 1;
}

int l_array_builder_append_array_builder_begin(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    if (!builder) { lua_pushnil(L); lua_pushstring(L, "invalid builder"); return 2; }
    void* child = nullptr;
    if (!builder->AppendArrayBuilderBegin(&child)) {
        lua_pushnil(L); lua_pushstring(L, "append array builder failed"); return 2;
    }
    lua_pushlightuserdata(L, child);
    return 1;
}

int l_array_builder_append_array_builder_end(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    void* child = lua_touserdata(L, 2);
    lua_pushboolean(L, builder && child &&
                    mongo::BsonArrayBuilder::AppendArrayBuilderEnd(builder, child));
    return 1;
}

const luaL_Reg kLib[] = {
    {"array_builder_new", l_array_builder_new},
    {"array_builder_destroy", l_array_builder_destroy},
    {"array_builder_build", l_array_builder_build},
    {"array_builder_append_int32", l_array_builder_append_int32},
    {"array_builder_append_int64", l_array_builder_append_int64},
    {"array_builder_append_double", l_array_builder_append_double},
    {"array_builder_append_utf8", l_array_builder_append_utf8},
    {"array_builder_append_bool", l_array_builder_append_bool},
    {"array_builder_append_null", l_array_builder_append_null},
    {"array_builder_append_document", l_array_builder_append_document},
    {"array_builder_append_array", l_array_builder_append_array},
    {"array_builder_append_datetime", l_array_builder_append_datetime},
    {"array_builder_append_binary", l_array_builder_append_binary},
    {"array_builder_append_binary_uninit", l_array_builder_append_binary_uninit},
    {"array_builder_append_regex", l_array_builder_append_regex},
    {"array_builder_append_code", l_array_builder_append_code},
    {"array_builder_append_minkey", l_array_builder_append_minkey},
    {"array_builder_append_maxkey", l_array_builder_append_maxkey},
    {"array_builder_append_undefined", l_array_builder_append_undefined},
    {"array_builder_append_symbol", l_array_builder_append_symbol},
    {"array_builder_append_oid", l_array_builder_append_oid},
    {"array_builder_append_timestamp", l_array_builder_append_timestamp},
    {"array_builder_append_now_utc", l_array_builder_append_now_utc},
    {"array_builder_append_decimal128", l_array_builder_append_decimal128},
    {"array_builder_append_code_with_scope", l_array_builder_append_code_with_scope},
    {"array_builder_append_iter", l_array_builder_append_iter},
    {"array_builder_append_db_pointer", l_array_builder_append_db_pointer},
    {"array_builder_append_time_t", l_array_builder_append_time_t},
    {"array_builder_append_timeval", l_array_builder_append_timeval},
    {"array_builder_append_document_begin", l_array_builder_append_document_begin},
    {"array_builder_append_document_end", l_array_builder_append_document_end},
    {"array_builder_append_value", l_array_builder_append_value},
    {"array_builder_append_array_from_vector", l_array_builder_append_array_from_vector},
    {"array_builder_append_array_builder_begin", l_array_builder_append_array_builder_begin},
    {"array_builder_append_array_builder_end", l_array_builder_append_array_builder_end},
    {nullptr, nullptr},
};

} // namespace

void RegisterBsonArrayBuilderMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_array_builder_gc);
}

const luaL_Reg* GetBsonArrayBuilderLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
