#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_opts";

int l_bulk_write_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	delete opts;
	*CheckUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bulk_write_opts_new(lua_State* L) {
	auto* opts = new (std::nothrow) mongo::MongoBulkWriteOpts();
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_bulk_write_opts_destroy(lua_State* L) {
	l_bulk_write_opts_gc(L);
	return 0;
}

int l_bulk_write_opts_set_ordered(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	if (opts) opts->SetOrdered(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_bulk_write_opts_set_bypass_document_validation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	if (opts) opts->SetBypassDocumentValidation(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_bulk_write_opts_set_let(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetLet(*doc);
	return 0;
}

int l_bulk_write_opts_set_write_concern(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
	if (opts && concern) opts->SetWriteConcern(*concern);
	return 0;
}

int l_bulk_write_opts_set_verbose_results(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	if (opts) opts->SetVerboseResults(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_bulk_write_opts_set_extra(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (opts && doc) opts->SetExtra(*doc);
	return 0;
}

int l_bulk_write_opts_set_server_id(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	auto val = static_cast<uint32_t>(luaL_checkinteger(L, 2));
	if (opts) opts->SetServerId(val);
	return 0;
}

int l_bulk_write_opts_set_comment(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoBulkWriteOpts>(L, 1, kMetaName);
	void* comment = lua_touserdata(L, 2);
	if (opts) opts->SetComment(comment);
	return 0;
}

const luaL_Reg kLib[] = {
	{"bulk_write_opts_new", l_bulk_write_opts_new},
	{"bulk_write_opts_destroy", l_bulk_write_opts_destroy},
	{"bulk_write_opts_set_ordered", l_bulk_write_opts_set_ordered},
	{"bulk_write_opts_set_bypass_document_validation",
	 l_bulk_write_opts_set_bypass_document_validation},
	{"bulk_write_opts_set_let", l_bulk_write_opts_set_let},
	{"bulk_write_opts_set_write_concern", l_bulk_write_opts_set_write_concern},
	{"bulk_write_opts_set_verbose_results", l_bulk_write_opts_set_verbose_results},
	{"bulk_write_opts_set_extra", l_bulk_write_opts_set_extra},
	{"bulk_write_opts_set_server_id", l_bulk_write_opts_set_server_id},
	{"bulk_write_opts_set_comment", l_bulk_write_opts_set_comment},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bulk_write_opts_gc);
}

const luaL_Reg* GetMongoBulkWriteOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
