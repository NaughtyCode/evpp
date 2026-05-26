#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_session.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_session.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.session";

int l_gc(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (session) session->Destroy();
    *CheckUserdata<mongo::MongoSession>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_start_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    bool ok = session->StartTransaction(nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_commit_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); lua_pushstring(L, "no session"); return 2; }
    mongo::MongoError error;
    bool ok = session->CommitTransaction(nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_abort_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); lua_pushstring(L, "no session"); return 2; }
    mongo::MongoError error;
    bool ok = session->AbortTransaction(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_in_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushboolean(L, session && session->InTransaction());
    return 1;
}

int l_get_server_id(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushinteger(L, session ? session->GetServerId() : 0);
    return 1;
}

int l_get_dirty(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushboolean(L, session && session->GetDirty());
    return 1;
}

int l_get_transaction_state(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushinteger(L, session ? static_cast<int>(session->GetTransactionState()) : 0);
    return 1;
}

const luaL_Reg kLib[] = {
    {"session_destroy", l_destroy},
    {"session_start_transaction", l_start_transaction},
    {"session_commit_transaction", l_commit_transaction},
    {"session_abort_transaction", l_abort_transaction},
    {"session_in_transaction", l_in_transaction},
    {"session_get_server_id", l_get_server_id},
    {"session_get_dirty", l_get_dirty},
    {"session_get_transaction_state", l_get_transaction_state},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoSessionMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoSessionLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
