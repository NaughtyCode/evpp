#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_error.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.error";

int l_error_gc(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(err);
	*CheckUserdata<mongo::MongoError>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_error_new(lua_State* L) {
	auto* err = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoError);
	if (!err) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoError>(L, kMetaName);
	*ud = err;
	return 1;
}

int l_error_destroy(lua_State* L) {
	l_error_gc(L);
	return 0;
}

int l_error_clear(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	if (err) err->Clear();
	return 0;
}

int l_error_domain(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	lua_pushinteger(L, err ? err->Domain() : 0);
	return 1;
}

int l_error_code(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	lua_pushinteger(L, err ? err->Code() : 0);
	return 1;
}

int l_error_message(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	const char* msg = err ? err->Message() : nullptr;
	if (msg)
		lua_pushstring(L, msg);
	else
		lua_pushnil(L);
	return 1;
}

int l_error_has_label(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	auto* reply = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	const char* label = luaL_checkstring(L, 3);
	lua_pushboolean(L, err && reply && err->HasLabel(*reply, label));
	return 1;
}

int l_error_set_error(lua_State* L) {
	auto* err = GetUserdata<mongo::MongoError>(L, 1, kMetaName);
	uint32_t domain = static_cast<uint32_t>(luaL_checkinteger(L, 2));
	uint32_t code = static_cast<uint32_t>(luaL_checkinteger(L, 3));
	const char* msg = luaL_checkstring(L, 4);
	if (err) err->SetError(domain, code, "%s", msg);
	return 0;
}

int l_error_strerror_r(lua_State* L) {
	int errno_val = static_cast<int>(luaL_checkinteger(L, 1));
	lua_pushstring(L, mongo::MongoError::StrErrorR(errno_val));
	return 1;
}

const luaL_Reg kLib[] = {
	{"error_new", l_error_new},
	{"error_destroy", l_error_destroy},
	{"error_clear", l_error_clear},
	{"error_domain", l_error_domain},
	{"error_code", l_error_code},
	{"error_message", l_error_message},
	{"error_has_label", l_error_has_label},
	{"error_set_error", l_error_set_error},
	{"error_strerror_r", l_error_strerror_r},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoErrorMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_error_gc);
}

const luaL_Reg* GetMongoErrorLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
