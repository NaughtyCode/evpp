#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_read_prefs.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.read_prefs";

int l_gc(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    delete prefs;
    *CheckUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto mode = static_cast<mongo::MongoReadPrefs::Mode>(luaL_optinteger(L, 1, 0));
    auto* prefs = new (std::nothrow) mongo::MongoReadPrefs(mode);
    if (!prefs) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoReadPrefs>(L, kMetaName);
    *ud = prefs;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_get_mode(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    lua_pushinteger(L, prefs ? static_cast<int>(prefs->GetMode()) : 0);
    return 1;
}

int l_set_mode(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto mode = static_cast<mongo::MongoReadPrefs::Mode>(luaL_checkinteger(L, 2));
    if (prefs) prefs->SetMode(mode);
    return 0;
}

int l_set_max_staleness_seconds(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto secs = static_cast<int>(luaL_checkinteger(L, 2));
    if (prefs) prefs->SetMaxStalenessSeconds(secs);
    return 0;
}

int l_is_valid(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    lua_pushboolean(L, prefs && prefs->IsValid());
    return 1;
}

const luaL_Reg kLib[] = {
    {"read_prefs_new", l_new},
    {"read_prefs_destroy", l_destroy},
    {"read_prefs_get_mode", l_get_mode},
    {"read_prefs_set_mode", l_set_mode},
    {"read_prefs_set_max_staleness_seconds", l_set_max_staleness_seconds},
    {"read_prefs_is_valid", l_is_valid},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoReadPrefsMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoReadPrefsLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
