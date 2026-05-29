#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_replace_one_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_replace_one_opts";

int l_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_opts_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoBulkWriteReplaceOneOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_opts_destroy(lua_State* L) {
	l_opts_gc(L);
	return 0;
}

int l_opts_set_collation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetCollation(*doc);
	return 0;
}

int l_opts_set_hint(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName);
	void* hint = lua_touserdata(L, 2);
	if (opts && hint) opts->SetHint(hint);
	return 0;
}

int l_opts_set_upsert(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName);
	if (opts) opts->SetUpsert(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_opts_set_sort(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetSort(*doc);
	return 0;
}

const luaL_Reg kLib[] = {
	{"bulk_write_replace_one_opts_new", l_opts_new},
	{"bulk_write_replace_one_opts_destroy", l_opts_destroy},
	{"bulk_write_replace_one_opts_set_collation", l_opts_set_collation},
	{"bulk_write_replace_one_opts_set_hint", l_opts_set_hint},
	{"bulk_write_replace_one_opts_set_upsert", l_opts_set_upsert},
	{"bulk_write_replace_one_opts_set_sort", l_opts_set_sort},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteReplaceOneOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_opts_gc);
}

const luaL_Reg* GetMongoBulkWriteReplaceOneOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
