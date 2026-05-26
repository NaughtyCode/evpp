#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_session_opts.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_session.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.session_opts";

int l_gc(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    delete opts;
    *CheckUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_new(lua_State* L) {
    auto* opts = new (std::nothrow) mongo::MongoSessionOpts();
    if (!opts) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoSessionOpts>(L, kMetaName);
    *ud = opts;
    return 1;
}

int l_destroy(lua_State* L) { l_gc(L); return 0; }

int l_clone(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    if (!opts) { lua_pushnil(L); return 1; }
    auto* copy = new (std::nothrow) mongo::MongoSessionOpts(opts->Clone());
    if (!copy) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoSessionOpts>(L, kMetaName);
    *ud = copy;
    return 1;
}

int l_set_causal_consistency(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    if (opts) opts->SetCausalConsistency(lua_toboolean(L, 2) != 0);
    return 0;
}

int l_get_causal_consistency(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    lua_pushboolean(L, opts && opts->GetCausalConsistency());
    return 1;
}

int l_set_snapshot(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    if (opts) opts->SetSnapshot(lua_toboolean(L, 2) != 0);
    return 0;
}

int l_get_snapshot(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    lua_pushboolean(L, opts && opts->GetSnapshot());
    return 1;
}

int l_set_default_transaction_opts(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoSessionOpts>(L, 1, kMetaName);
    auto* txn = GetUserdata<mongo::MongoTransactionOpts>(L, 2, "mongoc.transaction_opts");
    if (opts && txn) opts->SetDefaultTransactionOpts(*txn);
    return 0;
}

const luaL_Reg kLib[] = {
    {"session_opts_new", l_new},
    {"session_opts_destroy", l_destroy},
    {"session_opts_clone", l_clone},
    {"session_opts_set_causal_consistency", l_set_causal_consistency},
    {"session_opts_get_causal_consistency", l_get_causal_consistency},
    {"session_opts_set_snapshot", l_set_snapshot},
    {"session_opts_get_snapshot", l_get_snapshot},
    {"session_opts_set_default_transaction_opts", l_set_default_transaction_opts},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoSessionOptsMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_gc);
}

const luaL_Reg* GetMongoSessionOptsLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
