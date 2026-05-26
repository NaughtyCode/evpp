#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_client_pool.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/database/mongo/mongo_uri.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.pool";

int l_pool_gc(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    if (pool) pool->Destroy();
    delete pool;
    *CheckUserdata<mongo::MongoClientPool>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_pool_new(lua_State* L) {
    const char* uri_str = luaL_checkstring(L, 1);
    auto uri = mongo::MongoUri::New(uri_str);
    auto* pool = mongo::MongoClientPool::New(uri);
    if (!pool) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create client pool");
        return 2;
    }
    auto** ud = NewUserdata<mongo::MongoClientPool>(L, kMetaName);
    *ud = pool;
    return 1;
}

int l_pool_destroy(lua_State* L) { l_pool_gc(L); return 0; }

int l_pool_pop(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    if (!pool) { lua_pushnil(L); return 1; }
    auto* client = pool->Pop();
    if (!client) { lua_pushnil(L); lua_pushstring(L, "pool pop failed"); return 2; }
    auto** ud = NewUserdata<mongo::MongoClient>(L, "mongoc.client");
    *ud = client;
    return 1;
}

int l_pool_push(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    auto* client = GetUserdata<mongo::MongoClient>(L, 2, "mongoc.client");
    if (pool && client) pool->Push(client);
    return 0;
}

int l_pool_set_max_size(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    auto size = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (pool) pool->SetMaxSize(size);
    return 0;
}

int l_pool_set_appname(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    const char* appname = luaL_checkstring(L, 2);
    if (pool) pool->SetAppname(appname);
    return 0;
}

int l_pool_try_pop(lua_State* L) {
    auto* pool = GetUserdata<mongo::MongoClientPool>(L, 1, kMetaName);
    if (!pool) { lua_pushnil(L); return 1; }
    auto* client = pool->TryPop();
    if (!client) { lua_pushnil(L); return 1; }
    auto** ud = NewUserdata<mongo::MongoClient>(L, "mongoc.client");
    *ud = client;
    return 1;
}

const luaL_Reg kLib[] = {
    {"pool_new", l_pool_new},
    {"pool_destroy", l_pool_destroy},
    {"pool_pop", l_pool_pop},
    {"pool_try_pop", l_pool_try_pop},
    {"pool_push", l_pool_push},
    {"pool_set_max_size", l_pool_set_max_size},
    {"pool_set_appname", l_pool_set_appname},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoClientPoolMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_pool_gc);
}

const luaL_Reg* GetMongoClientPoolLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
