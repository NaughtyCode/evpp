#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_index_model.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_index_model.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.index_model";

int l_index_model_gc(lua_State* L) {
	auto* m = GetUserdata<mongo::MongoIndexModel>(L, 1, kMetaName);
	if (m) {
		m->Destroy();
		delete m;
	}
	*CheckUserdata<mongo::MongoIndexModel>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_index_model_new(lua_State* L) {
	auto* keys = GetUserdata<mongo::BsonDocument>(L, 1, "bson.doc");
	auto* opts =
		lua_isnoneornil(L, 2) ? nullptr : GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!keys) {
		lua_pushnil(L);
		lua_pushstring(L, "keys document required");
		return 2;
	}
	auto* m = new (std::nothrow) mongo::MongoIndexModel(*keys, opts);
	if (!m) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoIndexModel>(L, kMetaName);
	*ud = m;
	return 1;
}

int l_index_model_destroy(lua_State* L) {
	l_index_model_gc(L);
	return 0;
}

int l_index_model_get_raw(lua_State* L) {
	auto* m = GetUserdata<mongo::MongoIndexModel>(L, 1, kMetaName);
	if (!m) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, m->Raw());
	return 1;
}

const luaL_Reg kLib[] = {
	{"index_model_new", l_index_model_new},
	{"index_model_destroy", l_index_model_destroy},
	{"index_model_get_raw", l_index_model_get_raw},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoIndexModelMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_index_model_gc);
}

const luaL_Reg* GetMongoIndexModelLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
