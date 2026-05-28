#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_result.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_result";

int l_bulk_write_result_gc(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	delete result;
	*CheckUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bulk_write_result_new(lua_State* L) {
	auto* result = new (std::nothrow) mongo::MongoBulkWriteResult();
	if (!result) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteResult>(L, kMetaName);
	*ud = result;
	return 1;
}

int l_bulk_write_result_destroy(lua_State* L) {
	l_bulk_write_result_gc(L);
	return 0;
}

int l_bulk_write_result_inserted_count(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->InsertedCount() : 0);
	return 1;
}

int l_bulk_write_result_upserted_count(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->UpsertedCount() : 0);
	return 1;
}

int l_bulk_write_result_matched_count(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->MatchedCount() : 0);
	return 1;
}

int l_bulk_write_result_modified_count(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->ModifiedCount() : 0);
	return 1;
}

int l_bulk_write_result_deleted_count(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->DeletedCount() : 0);
	return 1;
}

int l_bulk_write_result_server_id(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	lua_pushinteger(L, result ? result->ServerId() : 0);
	return 1;
}

int l_bwr_insert_results(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	if (!result) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(result->InsertResults());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = new (std::nothrow) mongo::BsonDocument(bson_get_data(raw), raw->len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_bwr_update_results(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	if (!result) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(result->UpdateResults());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = new (std::nothrow) mongo::BsonDocument(bson_get_data(raw), raw->len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_bwr_delete_results(lua_State* L) {
	auto* result = GetUserdata<mongo::MongoBulkWriteResult>(L, 1, kMetaName);
	if (!result) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(result->DeleteResults());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = new (std::nothrow) mongo::BsonDocument(bson_get_data(raw), raw->len);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

const luaL_Reg kLib[] = {
	{"bulk_write_result_new", l_bulk_write_result_new},
	{"bulk_write_result_destroy", l_bulk_write_result_destroy},
	{"bulk_write_result_inserted_count", l_bulk_write_result_inserted_count},
	{"bulk_write_result_upserted_count", l_bulk_write_result_upserted_count},
	{"bulk_write_result_matched_count", l_bulk_write_result_matched_count},
	{"bulk_write_result_modified_count", l_bulk_write_result_modified_count},
	{"bulk_write_result_deleted_count", l_bulk_write_result_deleted_count},
	{"bulk_write_result_server_id", l_bulk_write_result_server_id},
	{"bulk_write_result_insert_results", l_bwr_insert_results},
	{"bulk_write_result_update_results", l_bwr_update_results},
	{"bulk_write_result_delete_results", l_bwr_delete_results},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteResultMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bulk_write_result_gc);
}

const luaL_Reg* GetMongoBulkWriteResultLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
