#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_cursor.h"

#include <cstdint>
#include <memory>
#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_cursor.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.cursor";

int l_cursor_gc(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (cursor) cursor->Destroy();
	delete cursor;
	*CheckUserdata<mongo::MongoCursor>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_cursor_destroy(lua_State* L) {
	l_cursor_gc(L);
	return 0;
}

int l_cursor_next(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushboolean(L, false);
		return 1;
	}
	auto doc = std::make_unique<mongo::BsonDocument>();
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	bool ok = cursor->Next(doc.get());
	if (!ok) {
		lua_pop(L, 1);
		mongo::MongoError error;
		if (cursor->HasError(&error)) {
			lua_pushnil(L);
			lua_pushstring(L, error.Message());
			return 2;
		}
		lua_pushboolean(L, false);`r`n			return 1;`r`n		}`r`n		*ud = doc.release();`r`n		return 1;
}

int l_cursor_more(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushboolean(L, cursor && cursor->More());
	return 1;
}

int l_cursor_set_batch_size(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	auto val = luaL_checkinteger(L, 2);
	if (val < 0 || val > UINT32_MAX) return 0;
	if (cursor) cursor->SetBatchSize(static_cast<uint32_t>(val));
	return 0;
}

int l_cursor_set_limit(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	auto val = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (cursor) cursor->SetLimit(val);
	return 0;
}

int l_cursor_get_batch_size(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushinteger(L, cursor ? cursor->GetBatchSize() : 0);
	return 1;
}

int l_cursor_get_server_id(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushinteger(L, cursor ? cursor->GetServerId() : 0);
	return 1;
}

int l_cursor_get_id(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushinteger(L, cursor ? cursor->GetId() : 0);
	return 1;
}

int l_cursor_get_limit(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushinteger(L, cursor ? cursor->GetLimit() : 0);
	return 1;
}

int l_cursor_set_max_await_time_ms(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	auto val = luaL_checkinteger(L, 2);
	if (val < 0 || val > UINT32_MAX) return 0;
	if (cursor) cursor->SetMaxAwaitTimeMs(static_cast<uint32_t>(val));
	return 0;
}

int l_cursor_get_max_await_time_ms(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	lua_pushinteger(L, cursor ? cursor->GetMaxAwaitTimeMs() : 0);
	return 1;
}

int l_cursor_set_server_id(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	auto server_id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
	if (cursor) cursor->SetServerId(server_id);
	return 0;
}

int l_cursor_has_error(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	lua_pushboolean(L, cursor->HasError(&error));
	return 1;
}

int l_cursor_error_document(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushboolean(L, false);
		return 1;
	}
	mongo::MongoError error;
	const void* raw_doc = nullptr;
	bool has_err = cursor->ErrorDocument(&error, &raw_doc);
	lua_pushboolean(L, has_err);
	if (has_err)
		lua_pushstring(L, error.Message());
	else
		lua_pushnil(L);
	return 2;
}

int l_cursor_current(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	auto* raw = static_cast<const bson_t*>(cursor->Current());
	if (!raw) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = new (std::nothrow)
		mongo::BsonDocument(mongo::BsonDocument::NewFromData(bson_get_data(raw), raw->len));
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_cursor_get_host(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	void* host_out = nullptr;
	cursor->GetHost(&host_out);
	if (host_out)
		lua_pushlightuserdata(L, host_out);
	else
		lua_pushnil(L);
	return 1;
}

int l_cursor_new_from_command_reply(lua_State* L) {
	// Requires a client lightuserdata + reply BsonDocument
	void* client = lua_touserdata(L, 1);
	auto* reply = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 3) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 3, "bson.doc");
	if (!client || !reply) {
		lua_pushnil(L);
		return 1;
	}
	auto* cursor = mongo::MongoCursor::NewFromCommandReplyWithOpts(client, *reply, opts);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoCursor>(L, kMetaName);
	*ud = cursor;
	return 1;
}

int l_cursor_clone(lua_State* L) {
	auto* cursor = GetUserdata<mongo::MongoCursor>(L, 1, kMetaName);
	if (!cursor) {
		lua_pushnil(L);
		return 1;
	}
	auto* clone = cursor->Clone();
	if (!clone) {
		lua_pushnil(L);
		return 1;
	}
	auto** ud = NewUserdata<mongo::MongoCursor>(L, kMetaName);
	*ud = clone;
	return 1;
}

const luaL_Reg kLib[] = {
	{"cursor_destroy", l_cursor_destroy},
	{"cursor_new_from_command_reply", l_cursor_new_from_command_reply},
	{"cursor_next", l_cursor_next},
	{"cursor_more", l_cursor_more},
	{"cursor_set_batch_size", l_cursor_set_batch_size},
	{"cursor_set_limit", l_cursor_set_limit},
	{"cursor_get_batch_size", l_cursor_get_batch_size},
	{"cursor_get_server_id", l_cursor_get_server_id},
	{"cursor_get_id", l_cursor_get_id},
	{"cursor_get_limit", l_cursor_get_limit},
	{"cursor_set_max_await_time_ms", l_cursor_set_max_await_time_ms},
	{"cursor_get_max_await_time_ms", l_cursor_get_max_await_time_ms},
	{"cursor_current", l_cursor_current},
	{"cursor_clone", l_cursor_clone},
	{"cursor_set_server_id", l_cursor_set_server_id},
	{"cursor_has_error", l_cursor_has_error},
	{"cursor_error_document", l_cursor_error_document},
	{"cursor_get_host", l_cursor_get_host},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoCursorMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_cursor_gc);
}

const luaL_Reg* GetMongoCursorLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif

