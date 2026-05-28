#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_insert_one_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_insert_one_opts";

int l_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteInsertOneOpts>(L, 1, kMetaName);
	MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoBulkWriteInsertOneOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_opts_new(lua_State* L) {
	auto* opts = MEM_NEW_NOTHROW(mongo::MongoBulkWriteInsertOneOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteInsertOneOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_opts_destroy(lua_State* L) {
	l_opts_gc(L);
	return 0;
}

const luaL_Reg kLib[] = {
	{"bulk_write_insert_one_opts_new", l_opts_new},
	{"bulk_write_insert_one_opts_destroy", l_opts_destroy},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteInsertOneOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_opts_gc);
}

const luaL_Reg* GetMongoBulkWriteInsertOneOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
