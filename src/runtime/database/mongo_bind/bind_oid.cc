#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_oid.h"

#include <cstring>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "runtime/database/mongo/mongo_oid.h"

namespace engine {
namespace script {
namespace {

int l_oid_new(lua_State* L) {
    mongo::MongoOid oid;
    oid.Init();
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_oid_is_valid(lua_State* L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    mongo::MongoOid oid;
    lua_pushboolean(L, oid.IsValid(str, len));
    return 1;
}

int l_oid_from_string(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    mongo::MongoOid oid;
    if (!oid.IsValid(str, strlen(str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    oid.InitFromString(str);
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_oid_compare(lua_State* L) {
    const char* a_str = luaL_checkstring(L, 1);
    const char* b_str = luaL_checkstring(L, 2);
    mongo::MongoOid a, b;
    if (!a.IsValid(a_str, strlen(a_str)) || !b.IsValid(b_str, strlen(b_str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    a.InitFromString(a_str);
    b.InitFromString(b_str);
    lua_pushinteger(L, a.Compare(b));
    return 1;
}

int l_oid_hash(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    mongo::MongoOid oid;
    if (!oid.IsValid(str, strlen(str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    oid.InitFromString(str);
    lua_pushinteger(L, oid.Hash());
    return 1;
}

int l_oid_get_time(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    mongo::MongoOid oid;
    if (!oid.IsValid(str, strlen(str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    oid.InitFromString(str);
    lua_pushinteger(L, static_cast<lua_Integer>(oid.GetTimeT()));
    return 1;
}

int l_oid_init_from_data(lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    if (len != 12) {
        lua_pushnil(L);
        lua_pushstring(L, "OID data must be exactly 12 bytes");
        return 2;
    }
    mongo::MongoOid oid;
    oid.InitFromData(reinterpret_cast<const uint8_t*>(data));
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_oid_get_bytes(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    mongo::MongoOid oid;
    if (!oid.IsValid(str, strlen(str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    oid.InitFromString(str);
    const uint8_t* bytes = oid.GetBytes();
    lua_pushlstring(L, reinterpret_cast<const char*>(bytes), 12);
    return 1;
}

int l_oid_equal(lua_State* L) {
    const char* a_str = luaL_checkstring(L, 1);
    const char* b_str = luaL_checkstring(L, 2);
    mongo::MongoOid a, b;
    if (!a.IsValid(a_str, strlen(a_str)) || !b.IsValid(b_str, strlen(b_str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    a.InitFromString(a_str);
    b.InitFromString(b_str);
    lua_pushboolean(L, a.Equal(b));
    return 1;
}

int l_oid_set_bytes(lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    if (len != 12) {
        lua_pushnil(L);
        lua_pushstring(L, "expected 12 bytes");
        return 2;
    }
    mongo::MongoOid oid;
    oid.SetBytes(reinterpret_cast<const uint8_t*>(data));
    std::string s = oid.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_oid_copy(lua_State* L) {
    const char* src_str = luaL_checkstring(L, 1);
    mongo::MongoOid src;
    if (!src.IsValid(src_str, strlen(src_str))) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid OID string");
        return 2;
    }
    src.InitFromString(src_str);
    mongo::MongoOid dst;
    dst.Copy(src);
    std::string s = dst.ToString();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

const luaL_Reg kLib[] = {
    {"oid_new", l_oid_new},
    {"oid_is_valid", l_oid_is_valid},
    {"oid_from_string", l_oid_from_string},
    {"oid_compare", l_oid_compare},
    {"oid_hash", l_oid_hash},
    {"oid_get_time", l_oid_get_time},
    {"oid_equal", l_oid_equal},
    {"oid_init_from_data", l_oid_init_from_data},
    {"oid_get_bytes", l_oid_get_bytes},
    {"oid_set_bytes", l_oid_set_bytes},
    {"oid_copy", l_oid_copy},
    {nullptr, nullptr},
};

} // namespace

const luaL_Reg* GetOidLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
