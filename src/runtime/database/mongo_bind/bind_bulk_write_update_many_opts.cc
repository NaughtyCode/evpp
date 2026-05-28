#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_update_many_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_update_many_opts";

int l_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName);
	MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_opts_new(lua_State* L) {
	auto* opts = MEM_NEW_NOTHROW(mongo::MongoBulkWriteUpdateManyOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_opts_destroy(lua_State* L) {
	l_opts_gc(L);
	return 0;
}

int l_opts_set_array_filters(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetArrayFilters(*doc);
	return 0;
}

int l_opts_set_collation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetCollation(*doc);
	return 0;
}

int l_opts_set_hint(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName);
	void* hint = lua_touserdata(L, 2);
	if (opts && hint) opts->SetHint(hint);
	return 0;
}

int l_opts_set_upsert(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(L, 1, kMetaName);
	if (opts) opts->SetUpsert(lua_toboolean(L, 2) != 0);
	return 0;
}

const luaL_Reg kLib[] = {
	{"bulk_write_update_many_opts_new", l_opts_new},
	{"bulk_write_update_many_opts_destroy", l_opts_destroy},
	{"bulk_write_update_many_opts_set_array_filters", l_opts_set_array_filters},
	{"bulk_write_update_many_opts_set_collation", l_opts_set_collation},
	{"bulk_write_update_many_opts_set_hint", l_opts_set_hint},
	{"bulk_write_update_many_opts_set_upsert", l_opts_set_upsert},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteUpdateManyOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_opts_gc);
}

const luaL_Reg* GetMongoBulkWriteUpdateManyOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
