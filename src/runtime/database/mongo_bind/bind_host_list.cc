#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_host_list.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_host_list.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.host_list";

int l_host_list_gc(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    delete hl;
    *CheckUserdata<mongo::MongoHostList>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_host_list_new(lua_State* L) {
    auto* hl = new (std::nothrow) mongo::MongoHostList();
    if (!hl) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoHostList>(L, kMetaName);
    *ud = hl;
    return 1;
}

int l_host_list_destroy(lua_State* L) { l_host_list_gc(L); return 0; }

int l_host_list_get_host(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    const char* s = hl ? hl->GetHost() : nullptr;
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_host_list_get_host_and_port(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    const char* s = hl ? hl->GetHostAndPort() : nullptr;
    if (s) lua_pushstring(L, s);
    else lua_pushnil(L);
    return 1;
}

int l_host_list_get_port(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    lua_pushinteger(L, hl ? hl->GetPort() : 0);
    return 1;
}

int l_host_list_get_family(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    lua_pushinteger(L, hl ? hl->GetFamily() : 0);
    return 1;
}

int l_host_list_get_next(lua_State* L) {
    auto* hl = GetUserdata<mongo::MongoHostList>(L, 1, kMetaName);
    if (!hl) { lua_pushnil(L); return 1; }
    auto* next = hl->GetNext();
    if (!next) { lua_pushnil(L); return 1; }
    // next is owned by the parent list, do not GC — wrap as non-owning pointer
    auto** ud = NewUserdata<mongo::MongoHostList>(L, kMetaName);
    *ud = next;
    return 1;
}

const luaL_Reg kLib[] = {
    {"host_list_new", l_host_list_new},
    {"host_list_destroy", l_host_list_destroy},
    {"host_list_get_host", l_host_list_get_host},
    {"host_list_get_host_and_port", l_host_list_get_host_and_port},
    {"host_list_get_port", l_host_list_get_port},
    {"host_list_get_family", l_host_list_get_family},
    {"host_list_get_next", l_host_list_get_next},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoHostListMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_host_list_gc);
}

const luaL_Reg* GetMongoHostListLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
