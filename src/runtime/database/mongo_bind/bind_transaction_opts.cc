#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_transaction_opts.h"
#include "runtime/database/mongo_bind/bind_util.h"

#include <new>

#include "runtime/database/mongo/mongo_session.h"
#include "runtime/database/mongo/mongo_settings.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.transaction_opts";

int l_txn_opts_gc(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    delete opts;
    *CheckUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName) = nullptr;
    return 0;
}

int l_txn_opts_new(lua_State* L) {
    auto* opts = new (std::nothrow) mongo::MongoTransactionOpts();
    if (!opts) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoTransactionOpts>(L, kMetaName);
    *ud = opts;
    return 1;
}

int l_txn_opts_destroy(lua_State* L) { l_txn_opts_gc(L); return 0; }

int l_txn_opts_clone(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    if (!opts) { lua_pushnil(L); return 1; }
    auto* copy = new (std::nothrow) mongo::MongoTransactionOpts(opts->Clone());
    if (!copy) { lua_pushnil(L); lua_pushstring(L, "allocation failure"); return 2; }
    auto** ud = NewUserdata<mongo::MongoTransactionOpts>(L, kMetaName);
    *ud = copy;
    return 1;
}

int l_txn_opts_set_max_commit_time_ms(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
    if (opts) opts->SetMaxCommitTimeMs(val);
    return 0;
}

int l_txn_opts_get_max_commit_time_ms(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    lua_pushinteger(L, opts ? opts->GetMaxCommitTimeMs() : 0);
    return 1;
}

int l_txn_opts_set_read_concern(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 2, "mongoc.read_concern");
    if (opts && concern) opts->SetReadConcern(*concern);
    return 0;
}

int l_txn_opts_set_write_concern(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
    if (opts && concern) opts->SetWriteConcern(*concern);
    return 0;
}

int l_txn_opts_set_read_prefs(lua_State* L) {
    auto* opts = GetUserdata<mongo::MongoTransactionOpts>(L, 1, kMetaName);
    auto* prefs = GetUserdata<mongo::MongoReadPrefs>(L, 2, "mongoc.read_prefs");
    if (opts && prefs) opts->SetReadPrefs(*prefs);
    return 0;
}

const luaL_Reg kLib[] = {
    {"transaction_opts_new", l_txn_opts_new},
    {"transaction_opts_destroy", l_txn_opts_destroy},
    {"transaction_opts_clone", l_txn_opts_clone},
    {"transaction_opts_set_max_commit_time_ms", l_txn_opts_set_max_commit_time_ms},
    {"transaction_opts_get_max_commit_time_ms", l_txn_opts_get_max_commit_time_ms},
    {"transaction_opts_set_read_concern", l_txn_opts_set_read_concern},
    {"transaction_opts_set_write_concern", l_txn_opts_set_write_concern},
    {"transaction_opts_set_read_prefs", l_txn_opts_set_read_prefs},
    {nullptr, nullptr},
};

} // namespace

void RegisterMongoTransactionOptsMeta(lua_State* L) {
    RegisterMetatable(L, kMetaName, nullptr, l_txn_opts_gc);
}

const luaL_Reg* GetMongoTransactionOptsLib() { return kLib; }

} // namespace script
} // namespace engine

#endif
