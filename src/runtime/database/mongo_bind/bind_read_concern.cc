#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_read_concern.h"

#include <new>

#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.read_concern";

int l_read_concern_gc(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	MEM_DELETE(concern);
	*CheckUserdata<mongo::MongoReadConcern>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_read_concern_new(lua_State* L) {
	auto* concern = MEM_NEW_NOTHROW(mongo::MongoReadConcern);
	if (!concern) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoReadConcern>(L, kMetaName);
	*ud = concern;
	return 1;
}

int l_read_concern_destroy(lua_State* L) {
	l_read_concern_gc(L);
	return 0;
}

int l_read_concern_get_level(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	if (!concern) {
		lua_pushnil(L);
		return 1;
	}
	const char* level = concern->GetLevel();
	if (level)
		lua_pushstring(L, level);
	else
		lua_pushnil(L);
	return 1;
}

int l_read_concern_set_level(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	const char* level = luaL_checkstring(L, 2);
	lua_pushboolean(L, concern && concern->SetLevel(level));
	return 1;
}

int l_read_concern_copy(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	if (!concern) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = MEM_NEW_NOTHROW(mongo::MongoReadConcern, concern->Copy());
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoReadConcern>(L, kMetaName);
	*ud = copy;
	return 1;
}

int l_read_concern_is_default(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->IsDefault());
	return 1;
}

int l_read_concern_append_to_opts(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoReadConcern>(L, 1, kMetaName);
	auto* opts = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!concern || !opts) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, concern->AppendToOpts(*opts));
	return 1;
}

const luaL_Reg kLib[] = {
	{"read_concern_new", l_read_concern_new},
	{"read_concern_destroy", l_read_concern_destroy},
	{"read_concern_copy", l_read_concern_copy},
	{"read_concern_get_level", l_read_concern_get_level},
	{"read_concern_set_level", l_read_concern_set_level},
	{"read_concern_is_default", l_read_concern_is_default},
	{"read_concern_append_to_opts", l_read_concern_append_to_opts},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoReadConcernMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_read_concern_gc);
}

const luaL_Reg* GetMongoReadConcernLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
