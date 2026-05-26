#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_read_concern.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.read_concern";

int l_gc(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
    delete concern;
    *CheckUserdata<mongo::MongoReadConcern>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* concern = new (std::nothrow) mongo::MongoReadConcern();
    if (!concern) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoReadConcern>(L, kMetaName);
    *ud = concern;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_get_level(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
    if (!concern) { lua_pushnil(L); return 1; }
    const char* level = concern->GetLevel();
    if (level) lua_pushstring(L, level);
    else lua_pushnil(L);
    return 1;
}

int l_set_level(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
    const char* level = luaL_checkstring(L, 2);
    lua_pushboolean(L, concern && concern->SetLevel(level));
    return 1;
}

int l_is_default(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
    lua_pushboolean(L, concern && concern->IsDefault());
    return 1;
}

int l_append_to_opts(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
    auto* opts = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!concern || !opts) { lua_pushboolean(L, false); return 1; }
    lua_pushboolean(L, concern->AppendToOpts(*opts));
    return 1;
}

const luaL_Reg kLib[] = {
    {"read_concern_new", l_new},
    {"read_concern_destroy", l_destroy},
    {"read_concern_get_level", l_get_level},
    {"read_concern_set_level", l_set_level},
    {"read_concern_is_default", l_is_default},
    {"read_concern_append_to_opts", l_append_to_opts},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoReadConcernMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoReadConcernLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
