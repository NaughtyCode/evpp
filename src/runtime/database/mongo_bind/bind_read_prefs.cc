#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_read_prefs.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include <bson/bson.h>
#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.read_prefs";

int l_read_prefs_gc(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    delete prefs;
    *CheckUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_read_prefs_new(lua_State* L) {
    auto mode = static_cast<mongo::MongoReadPrefs::Mode>(luaL_optinteger(L, 1, 0));
    auto* prefs = new (std::nothrow) mongo::MongoReadPrefs(mode);
    if (!prefs) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoReadPrefs>(L, kMetaName);
    *ud = prefs;
    return 1;
}

int l_read_prefs_destroy(lua_State* L) { l_read_prefs_gc(L); return 0; }

int l_read_prefs_get_mode(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    lua_pushinteger(L, prefs ? static_cast<int>(prefs->GetMode()) : 0);
    return 1;
}

int l_read_prefs_set_mode(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto mode = static_cast<mongo::MongoReadPrefs::Mode>(luaL_checkinteger(L, 2));
    if (prefs) prefs->SetMode(mode);
    return 0;
}

int l_read_prefs_set_max_staleness_seconds(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto secs = static_cast<int>(luaL_checkinteger(L, 2));
    if (prefs) prefs->SetMaxStalenessSeconds(secs);
    return 0;
}

int l_read_prefs_is_valid(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    lua_pushboolean(L, prefs && prefs->IsValid());
    return 1;
}

int l_read_prefs_copy(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    if (!prefs) { lua_pushnil(L); return 1; }
    auto* copy = new (std::nothrow) mongo::MongoReadPrefs(prefs->Copy());
    if (!copy) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoReadPrefs>(L, kMetaName);
    *ud = copy;
    return 1;
}

int l_read_prefs_get_max_staleness(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    lua_pushinteger(L, prefs ? prefs->GetMaxStalenessSeconds() : 0);
    return 1;
}

int l_read_prefs_set_tags(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto* tags = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (prefs && tags) prefs->SetTags(*tags);
    return 0;
}

int l_read_prefs_add_tag(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto* tag = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (prefs && tag) prefs->AddTag(*tag);
    return 0;
}

int l_read_prefs_set_hedge(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    auto* hedge = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (prefs && hedge) prefs->SetHedge(*hedge);
    return 0;
}

int l_read_prefs_get_tags(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    if (!prefs) { lua_pushnil(L); return 1; }
    auto* raw = static_cast<const bson_t*>(prefs->GetTags());
    if (!raw) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument(bson_get_data(raw), raw->len);
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

int l_read_prefs_get_hedge(lua_State* L) {
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 1, kMetaName);
    if (!prefs) { lua_pushnil(L); return 1; }
    auto* raw = static_cast<const bson_t*>(prefs->GetHedge());
    if (!raw) { lua_pushnil(L); return 1; }
    auto* doc = new (std::nothrow) mongo::BsonDocument(bson_get_data(raw), raw->len);
    if (!doc) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
    *ud = doc;
    return 1;
}

const luaL_Reg kLib[] = {
    {"read_prefs_new", l_read_prefs_new},
    {"read_prefs_destroy", l_read_prefs_destroy},
    {"read_prefs_get_mode", l_read_prefs_get_mode},
    {"read_prefs_set_mode", l_read_prefs_set_mode},
    {"read_prefs_set_max_staleness_seconds", l_read_prefs_set_max_staleness_seconds},
    {"read_prefs_get_max_staleness_seconds", l_read_prefs_get_max_staleness},
    {"read_prefs_set_tags", l_read_prefs_set_tags},
    {"read_prefs_add_tag", l_read_prefs_add_tag},
    {"read_prefs_set_hedge", l_read_prefs_set_hedge},
    {"read_prefs_get_tags", l_read_prefs_get_tags},
    {"read_prefs_get_hedge", l_read_prefs_get_hedge},
    {"read_prefs_copy", l_read_prefs_copy},
    {"read_prefs_is_valid", l_read_prefs_is_valid},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoReadPrefsMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_read_prefs_gc);
}

const luaL_Reg* GetMongoReadPrefsLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
