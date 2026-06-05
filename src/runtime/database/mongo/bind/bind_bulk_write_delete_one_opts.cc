#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_bulk_write_delete_one_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_delete_one_opts";

int l_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteDeleteOneOpts>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoBulkWriteDeleteOneOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoBulkWriteDeleteOneOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteDeleteOneOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_opts_destroy(lua_State* L) {
	l_opts_gc(L);
	return 0;
}

int l_opts_set_collation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteDeleteOneOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetCollation(*doc);
	return 0;
}

int l_opts_set_hint(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteDeleteOneOpts>(L, 1, kMetaName);
	void* hint = lua_touserdata(L, 2);
	if (opts && hint) opts->SetHint(hint);
	return 0;
}

const luaL_Reg kLib[] = {
	{"bulk_write_delete_one_opts_new", l_opts_new},
	{"bulk_write_delete_one_opts_destroy", l_opts_destroy},
	{"bulk_write_delete_one_opts_set_collation", l_opts_set_collation},
	{"bulk_write_delete_one_opts_set_hint", l_opts_set_hint},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteDeleteOneOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_opts_gc);
}

const luaL_Reg* GetMongoBulkWriteDeleteOneOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
