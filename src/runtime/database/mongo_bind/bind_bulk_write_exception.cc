#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_bulk_write_exception.h"

#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_bulkwrite.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.bulk_write_exception";

int l_bulk_write_exc_gc(lua_State* L) {
	auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
	MEM_DELETE(exc);
	*CheckUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_bulk_write_exc_new(lua_State* L) {
	auto* exc = MEM_NEW_NOTHROW(mongo::MongoBulkWriteException);
	if (!exc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoBulkWriteException>(L, kMetaName);
	*ud = exc;
	return 1;
}

int l_bulk_write_exc_destroy(lua_State* L) {
	l_bulk_write_exc_gc(L);
	return 0;
}

int l_bulk_write_exc_error(lua_State* L) {
	auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
	if (!exc) {
		lua_pushboolean(L, false);
		lua_pushnil(L);
		lua_pushnil(L);
		return 3;
	}
	mongo::MongoError error;
	bool ok = exc->Error(&error);
	lua_pushboolean(L, ok);
	if (ok)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	lua_pushnil(L);
	return 3;
}

int l_bwe_write_errors(lua_State* L) {
	auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
	if (!exc) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(exc->WriteErrors());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = MEM_NEW_NOTHROW(mongo::BsonDocument, bson_get_data(raw), raw->len);
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

int l_bwe_write_concern_errors(lua_State* L) {
	auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
	if (!exc) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(exc->WriteConcernErrors());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = MEM_NEW_NOTHROW(mongo::BsonDocument, bson_get_data(raw), raw->len);
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

int l_bwe_error_reply(lua_State* L) {
	auto* exc = GetUserdata<mongo::MongoBulkWriteException>(L, 1, kMetaName);
	if (!exc) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(exc->ErrorReply());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = MEM_NEW_NOTHROW(mongo::BsonDocument, bson_get_data(raw), raw->len);
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
	{"bulk_write_exception_new", l_bulk_write_exc_new},
	{"bulk_write_exception_destroy", l_bulk_write_exc_destroy},
	{"bulk_write_exception_error", l_bulk_write_exc_error},
	{"bulk_write_exception_write_errors", l_bwe_write_errors},
	{"bulk_write_exception_write_concern_errors", l_bwe_write_concern_errors},
	{"bulk_write_exception_error_reply", l_bwe_error_reply},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoBulkWriteExceptionMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_bulk_write_exc_gc);
}

const luaL_Reg* GetMongoBulkWriteExceptionLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
