#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_operation.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulk.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk";

int l_bulk_gc(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	if (bulk) bulk->Destroy();
	*CheckUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bulk_new(lua_State* L) {
	bool ordered = lua_toboolean(L, 1) != 0;
	auto* bulk = mongo::MongoBulkOperation::New(ordered);
	if (!bulk) {
		lua_pushnil(L);
		lua_pushstring(L, "failed to create bulk operation");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkOperation>(L, kMetaName);
	*ud = bulk;
	return 1;
}

int l_bulk_destroy(lua_State* L) {
	l_bulk_gc(L);
	return 0;
}

int l_bulk_insert(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (bulk && doc) bulk->Insert(*doc);
	return 0;
}

int l_bulk_remove_one(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (bulk && selector) bulk->RemoveOne(*selector);
	return 0;
}

int l_bulk_update_one(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* update = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	bool upsert = lua_toboolean(L, 4) != 0;
	if (bulk && selector && update) bulk->UpdateOne(*selector, *update, upsert);
	return 0;
}

int l_bulk_replace_one(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	bool upsert = lua_toboolean(L, 4) != 0;
	if (bulk && selector && doc) bulk->ReplaceOne(*selector, *doc, upsert);
	return 0;
}

int l_bulk_execute(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	if (!bulk) {
		lua_pushnil(L);
		lua_pushstring(L, "no bulk operation");
		lua_pushnil(L);
		return 3;
	}
	mongo::BsonDocument reply;
	mongo::MongoError error;
	uint32_t server_id = bulk->Execute(&reply, &error);
	if (server_id == 0) {
		lua_pushnil(L);
		lua_pushstring(L, error.Message());
		lua_pushnil(L);
		return 3;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument, std::move(reply));
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	lua_pushinteger(L, server_id);
	lua_pushnil(L);
	return 3;
}

int l_bulk_set_write_concern(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 2, "mongoc.write_concern");
	if (bulk && concern) bulk->SetWriteConcern(*concern);
	return 0;
}

int l_bulk_set_comment(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	void* comment = lua_touserdata(L, 2);
	if (bulk) bulk->SetComment(comment);
	return 0;
}

int l_bulk_set_client(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	void* client = lua_touserdata(L, 2);
	if (bulk) bulk->SetClient(client);
	return 0;
}

int l_bulk_set_client_session(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	void* session = lua_touserdata(L, 2);
	if (bulk) bulk->SetClientSession(session);
	return 0;
}

int l_bulk_update(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* document = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	bool upsert = lua_toboolean(L, 4) != 0;
	if (bulk && selector && document) bulk->Update(*selector, *document, upsert);
	return 0;
}

int l_bulk_remove(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (bulk && selector) bulk->Remove(*selector);
	return 0;
}

int l_bulk_set_bypass_document_validation(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	bool bypass = lua_toboolean(L, 2) != 0;
	if (bulk) bulk->SetBypassDocumentValidation(bypass);
	return 0;
}

int l_bulk_set_let(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* let = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (bulk && let) bulk->SetLet(*let);
	return 0;
}

int l_bulk_insert_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bulk || !doc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->InsertWithOpts(*doc, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_remove_one_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bulk || !selector) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->RemoveOneWithOpts(*selector, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_remove_many_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!bulk || !selector) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->RemoveManyWithOpts(*selector, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_replace_one_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bulk || !selector || !doc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->ReplaceOneWithOpts(*selector, *doc, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_update_one_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bulk || !selector || !doc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->UpdateOneWithOpts(*selector, *doc, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_update_many_with_opts(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto* selector = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 4) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 4, "bson.doc");
	if (!bulk || !selector || !doc) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "invalid args");
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	lua_pushboolean(L, bulk->UpdateManyWithOpts(*selector, *doc, opts, &error));
	if (!lua_toboolean(L, -1))
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bulk_set_server_id(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	auto server_id = CheckIntegerArg<uint32_t>(L, 2);
	if (bulk) bulk->SetServerId(server_id);
	return 0;
}

int l_bulk_get_server_id(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	lua_pushinteger(L, bulk ? bulk->GetServerId() : 0);
	return 1;
}

int l_bulk_set_database(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	const char* db_name = luaL_checkstring(L, 2);
	if (bulk) bulk->SetDatabase(db_name);
	return 0;
}

int l_bulk_set_collection(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	const char* coll_name = luaL_checkstring(L, 2);
	if (bulk) bulk->SetCollection(coll_name);
	return 0;
}

int l_bulk_op_get_write_concern(lua_State* L) {
	auto* bulk = GetUserdata<mongo::MongoBulkOperation>(L, 1, kMetaName);
	if (!bulk) {
		lua_pushnil(L);
		return 1;
	}
	const void* wc = bulk->GetWriteConcern();
	if (!wc) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, const_cast<void*>(wc));
	return 1;
}

const luaL_Reg kLib[] = {
	{"bulk_new", l_bulk_new},
	{"bulk_destroy", l_bulk_destroy},
	{"bulk_insert", l_bulk_insert},
	{"bulk_update", l_bulk_update},
	{"bulk_remove", l_bulk_remove},
	{"bulk_remove_one", l_bulk_remove_one},
	{"bulk_update_one", l_bulk_update_one},
	{"bulk_replace_one", l_bulk_replace_one},
	{"bulk_execute", l_bulk_execute},
	{"bulk_set_write_concern", l_bulk_set_write_concern},
	{"bulk_set_comment", l_bulk_set_comment},
	{"bulk_set_client", l_bulk_set_client},
	{"bulk_set_client_session", l_bulk_set_client_session},
	{"bulk_set_bypass_document_validation", l_bulk_set_bypass_document_validation},
	{"bulk_set_let", l_bulk_set_let},
	{"bulk_insert_with_opts", l_bulk_insert_with_opts},
	{"bulk_remove_one_with_opts", l_bulk_remove_one_with_opts},
	{"bulk_remove_many_with_opts", l_bulk_remove_many_with_opts},
	{"bulk_replace_one_with_opts", l_bulk_replace_one_with_opts},
	{"bulk_update_one_with_opts", l_bulk_update_one_with_opts},
	{"bulk_update_many_with_opts", l_bulk_update_many_with_opts},
	{"bulk_set_server_id", l_bulk_set_server_id},
	{"bulk_get_server_id", l_bulk_get_server_id},
	{"bulk_set_database", l_bulk_set_database},
	{"bulk_set_collection", l_bulk_set_collection},
	{"bulk_op_get_write_concern", l_bulk_op_get_write_concern},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkOperationMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bulk_gc);
}

const luaL_Reg* GetMongoBulkOperationLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
