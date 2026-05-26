#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_session.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_session.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.session";

int l_session_gc(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (session) session->Destroy();
    *CheckUserdata<mongo::MongoSession>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_session_destroy(lua_State* L) { l_session_gc(L); return 0; }

int l_session_start_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    bool ok = session->StartTransaction(nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_session_commit_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); lua_pushstring(L, "no session"); return 2; }
    mongo::MongoError error;
    bool ok = session->CommitTransaction(nullptr, &error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_session_abort_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    if (!session) { lua_pushboolean(L, false); lua_pushstring(L, "no session"); return 2; }
    mongo::MongoError error;
    bool ok = session->AbortTransaction(&error);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, error.Message());
    else lua_pushnil(L);
    return 2;
}

int l_session_in_transaction(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushboolean(L, session && session->InTransaction());
    return 1;
}

int l_session_get_server_id(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushinteger(L, session ? session->GetServerId() : 0);
    return 1;
}

int l_session_get_dirty(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushboolean(L, session && session->GetDirty());
    return 1;
}

int l_session_get_transaction_state(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    lua_pushinteger(L, session ? static_cast<int>(session->GetTransactionState()) : 0);
    return 1;
}

int l_session_append_to_opts(lua_State* L) {
    auto* session = GetUserdata<mongo::MongoSession>(L, 1, kMetaName);
    auto* opts = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
    if (!session || !opts) { lua_pushboolean(L, false); return 1; }
    mongo::MongoError error;
    lua_pushboolean(L, session->AppendToOpts(opts, &error));
    return 1;
}

const luaL_Reg kLib[] = {
    {"session_destroy", l_session_destroy},
    {"session_start_transaction", l_session_start_transaction},
    {"session_commit_transaction", l_session_commit_transaction},
    {"session_abort_transaction", l_session_abort_transaction},
    {"session_in_transaction", l_session_in_transaction},
    {"session_get_server_id", l_session_get_server_id},
    {"session_get_dirty", l_session_get_dirty},
    {"session_get_transaction_state", l_session_get_transaction_state},
    {"session_append_to_opts", l_session_append_to_opts},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoSessionMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_session_gc);
}

const luaL_Reg* GetMongoSessionLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
