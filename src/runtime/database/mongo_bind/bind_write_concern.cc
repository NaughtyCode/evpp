#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_write_concern.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.write_concern";

int l_gc(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    delete concern;
    *CheckUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* concern = new (std::nothrow) mongo::MongoWriteConcern();
    if (!concern) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoWriteConcern>(L, kMetaName);
    *ud = concern;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_get_w(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushinteger(L, concern ? concern->GetW() : 0);
    return 1;
}

int l_set_w(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    auto w = static_cast<int32_t>(luaL_checkinteger(L, 2));
    if (concern) concern->SetW(w);
    return 0;
}

int l_get_journal(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushboolean(L, concern && concern->GetJournal());
    return 1;
}

int l_set_journal(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    bool journal = lua_toboolean(L, 2) != 0;
    if (concern) concern->SetJournal(journal);
    return 0;
}

int l_get_w_timeout(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushinteger(L, concern ? concern->GetWTimeout() : 0);
    return 1;
}

int l_set_w_timeout(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    auto timeout = static_cast<int32_t>(luaL_checkinteger(L, 2));
    if (concern) concern->SetWTimeout(timeout);
    return 0;
}

int l_is_acknowledged(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushboolean(L, concern && concern->IsAcknowledged());
    return 1;
}

int l_is_valid(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushboolean(L, concern && concern->IsValid());
    return 1;
}

int l_is_default(lua_State* L) {
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
    lua_pushboolean(L, concern && concern->IsDefault());
    return 1;
}

const luaL_Reg kLib[] = {
    {"write_concern_new", l_new},
    {"write_concern_destroy", l_destroy},
    {"write_concern_get_w", l_get_w},
    {"write_concern_set_w", l_set_w},
    {"write_concern_get_journal", l_get_journal},
    {"write_concern_set_journal", l_set_journal},
    {"write_concern_get_w_timeout", l_get_w_timeout},
    {"write_concern_set_w_timeout", l_set_w_timeout},
    {"write_concern_is_acknowledged", l_is_acknowledged},
    {"write_concern_is_valid", l_is_valid},
    {"write_concern_is_default", l_is_default},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoWriteConcernMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoWriteConcernLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
