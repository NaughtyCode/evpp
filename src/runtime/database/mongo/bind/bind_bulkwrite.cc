#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_bulkwrite.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulkwrite";

int l_bulkwrite_gc(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	if (bw) bw->Destroy();
	*CheckUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bulkwrite_new(lua_State* L) {
	auto* bw = mongo::MongoBulkWrite::New();
	if (!bw) {
		lua_pushnil(L);
		lua_pushstring(L, "failed to create bulk write");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWrite>(L, kMetaName);
	*ud = bw;
	return 1;
}

int l_bulkwrite_destroy(lua_State* L) {
	l_bulkwrite_gc(L);
	return 0;
}

int l_bulkwrite_append_insert_one(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bw || !doc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	auto* ins_opts = lua_isnoneornil(L, 4) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteInsertOneOpts>(
												 L, 4, "mongoc.bulk_write_insert_one_opts");
	mongo::MongoError error;
	bool ok = bw->AppendInsertOne(ns, *doc, ins_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_append_update_one(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* update = GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bw || !filter || !update) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* upd_opts = lua_isnoneornil(L, 5) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteUpdateOneOpts>(
												 L, 5, "mongoc.bulk_write_update_one_opts");
	bool ok = bw->AppendUpdateOne(ns, *filter, *update, upd_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_append_delete_one(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bw || !filter) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* del_opts = lua_isnoneornil(L, 4) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteDeleteOneOpts>(
												 L, 4, "mongoc.bulk_write_delete_one_opts");
	bool ok = bw->AppendDeleteOne(ns, *filter, del_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_append_update_many(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* update = GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bw || !filter || !update) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* upm_opts = lua_isnoneornil(L, 5) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteUpdateManyOpts>(
												 L, 5, "mongoc.bulk_write_update_many_opts");
	bool ok = bw->AppendUpdateMany(ns, *filter, *update, upm_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_append_replace_one(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* replacement = GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bw || !filter || !replacement) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* rep_opts = lua_isnoneornil(L, 5) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteReplaceOneOpts>(
												 L, 5, "mongoc.bulk_write_replace_one_opts");
	bool ok = bw->AppendReplaceOne(ns, *filter, *replacement, rep_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_append_delete_many(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	const char* ns = luaL_checkstring(L, 2);
	auto* filter = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bw || !filter) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto* dem_opts = lua_isnoneornil(L, 4) ? nullptr
										   : GetUserdata<mongo::MongoBulkWriteDeleteManyOpts>(
												 L, 4, "mongoc.bulk_write_delete_many_opts");
	bool ok = bw->AppendDeleteMany(ns, *filter, dem_opts, &error);
	lua_pushboolean(L, ok);
	if (!ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulkwrite_execute(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	if (!bw) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid bulk write");
		lua_pushnil(L);
		return 3;
	}

	auto* opts = lua_isnoneornil(L, 2)
					 ? nullptr
					 : GetUserdata<mongo::MongoBulkWriteOpts>(L, 2, "mongoc.bulk_write_opts");
	auto ret = bw->Execute(opts);

	if (ret.exception) {
		mongo::MongoError error;
		ret.exception->Error(&error);
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		CLOUDENGINE_MEM_DELETE(ret.exception);
		CLOUDENGINE_MEM_DELETE(ret.result);
		lua_pushnil(L);
		return 3;
	}

	auto* result_doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!result_doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		CLOUDENGINE_MEM_DELETE(ret.result);
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = result_doc;
	lua_pushinteger(L, ret.result ? ret.result->InsertedCount() : 0);
	lua_pushinteger(L, ret.result ? ret.result->DeletedCount() : 0);
	lua_pushinteger(L, ret.result ? ret.result->MatchedCount() : 0);
	lua_pushinteger(L, ret.result ? ret.result->ModifiedCount() : 0);
	lua_pushinteger(L, ret.result ? ret.result->UpsertedCount() : 0);
	CLOUDENGINE_MEM_DELETE(ret.result);
	return 6;
}

int l_bulk_write_new_from_client(lua_State* L) {
	void* raw_client = lua_touserdata(L, 1);
	if (!raw_client) {
		lua_pushnil(L);
		lua_pushstring(L, "raw client pointer required");
		lua_pushnil(L);
		return 3;
	}
	auto* bw = mongo::MongoBulkWrite::New(raw_client);
	if (!bw) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWrite>(L, kMetaName);
	*ud = bw;
	return 1;
}

int l_bulk_write_check_acknowledged(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	if (!bw) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid bulk write");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto ret = bw->CheckAcknowledged(&error);
	if (!ret.is_ok) {
		lua_pushboolean(L, false);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	lua_pushboolean(L, ret.is_acknowledged);
	lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_write_server_id(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	if (!bw) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid bulk write");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	auto ret = bw->ServerId(&error);
	if (!ret.is_ok) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	lua_pushinteger(L, ret.server_id);
	lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_write_set_session(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	void* session = lua_touserdata(L, 2);
	if (bw && session) bw->SetSession(session);
	return 0;
}

int l_bulk_write_set_client(lua_State* L) {
	auto* bw = GetUserdata<mongo::MongoBulkWrite>(L, 1, kMetaName);
	void* client = lua_touserdata(L, 2);
	lua_pushboolean(L, bw && client ? bw->SetClient(client) : false);
	return 1;
}

const luaL_Reg kLib[] = {
	{"bulkwrite_new", l_bulkwrite_new},
	{"bulkwrite_destroy", l_bulkwrite_destroy},
	{"bulkwrite_append_insert_one", l_bulkwrite_append_insert_one},
	{"bulkwrite_append_update_one", l_bulkwrite_append_update_one},
	{"bulkwrite_append_update_many", l_bulkwrite_append_update_many},
	{"bulkwrite_append_replace_one", l_bulkwrite_append_replace_one},
	{"bulkwrite_append_delete_one", l_bulkwrite_append_delete_one},
	{"bulkwrite_append_delete_many", l_bulkwrite_append_delete_many},
	{"bulkwrite_execute", l_bulkwrite_execute},
	{"bulk_write_new_from_client", l_bulk_write_new_from_client},
	{"bulk_write_check_acknowledged", l_bulk_write_check_acknowledged},
	{"bulk_write_server_id", l_bulk_write_server_id},
	{"bulk_write_set_session", l_bulk_write_set_session},
	{"bulk_write_set_client", l_bulk_write_set_client},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bulkwrite_gc);
}

const luaL_Reg* GetMongoBulkWriteLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
