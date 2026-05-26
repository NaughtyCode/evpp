#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_array_builder.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <cstdint>
#include <new>

#include "runtime/database/mongo/mongo_bson.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "bson.array_builder";

int l_gc(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    delete builder;
    *CheckUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* builder = new (std::nothrow) mongo::BsonArrayBuilder();
    if (!builder) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonArrayBuilder>(L, kMetaName);
    *ud = builder;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_build(lua_State* L) {
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

int l_append_int32(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int32_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendInt32(val));
    return 1;
}

int l_append_int64(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendInt64(val));
    return 1;
}

int l_append_double(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    double val = static_cast<double>(luaL_checknumber(L, 2));
    lua_pushboolean(L, builder && builder->AppendDouble(val));
    return 1;
}

int l_append_utf8(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendUtf8(val));
    return 1;
}

int l_append_bool(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    bool val = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, builder && builder->AppendBool(val));
    return 1;
}

int l_append_null(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendNull());
    return 1;
}

int l_append_document(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && doc && builder->AppendDocument(*doc));
    return 1;
}

int l_append_array(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    lua_pushboolean(L, builder && doc && builder->AppendArray(*doc));
    return 1;
}

int l_append_datetime(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, builder && builder->AppendDateTime(val));
    return 1;
}

int l_append_binary(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    auto subtype = static_cast<int>(luaL_checkinteger(L, 2));
    size_t len;
    const char* data = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, builder && len <= UINT32_MAX
                        && builder->AppendBinary(subtype, reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len)));
    return 1;
}

int l_append_regex(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* regex = luaL_checkstring(L, 2);
    const char* options = luaL_checkstring(L, 3);
    lua_pushboolean(L, builder && builder->AppendRegex(regex, options));
    return 1;
}

int l_append_code(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* code = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendCode(code));
    return 1;
}

int l_append_minkey(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendMinkey());
    return 1;
}

int l_append_maxkey(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendMaxkey());
    return 1;
}

int l_append_undefined(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendUndefined());
    return 1;
}

int l_append_symbol(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    const char* val = luaL_checkstring(L, 2);
    lua_pushboolean(L, builder && builder->AppendSymbol(val));
    return 1;
}

int l_append_now_utc(lua_State* L) {
    auto* builder = GetUserdata<mongo::BsonArrayBuilder>(L, 1, kMetaName);
    lua_pushboolean(L, builder && builder->AppendNowUtc());
    return 1;
}

int l_append_decimal128(lua_State* L) {
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

const luaL_Reg kLib[] = {
    {"array_builder_new", l_new},
    {"array_builder_destroy", l_destroy},
    {"array_builder_build", l_build},
    {"array_builder_append_int32", l_append_int32},
    {"array_builder_append_int64", l_append_int64},
    {"array_builder_append_double", l_append_double},
    {"array_builder_append_utf8", l_append_utf8},
    {"array_builder_append_bool", l_append_bool},
    {"array_builder_append_null", l_append_null},
    {"array_builder_append_document", l_append_document},
    {"array_builder_append_array", l_append_array},
    {"array_builder_append_datetime", l_append_datetime},
    {"array_builder_append_binary", l_append_binary},
    {"array_builder_append_regex", l_append_regex},
    {"array_builder_append_code", l_append_code},
    {"array_builder_append_minkey", l_append_minkey},
    {"array_builder_append_maxkey", l_append_maxkey},
    {"array_builder_append_undefined", l_append_undefined},
    {"array_builder_append_symbol", l_append_symbol},
    {"array_builder_append_now_utc", l_append_now_utc},
    {"array_builder_append_decimal128", l_append_decimal128},
    {nullptr, nullptr},
};

} // namespace

void RegisterBsonArrayBuilderMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetBsonArrayBuilderLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
