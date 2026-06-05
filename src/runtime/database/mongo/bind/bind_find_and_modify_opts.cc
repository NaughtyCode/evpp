#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_find_and_modify_opts.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_find_and_modify_opts.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.find_and_modify_opts";

int l_find_and_modify_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	CLOUDENGINE_MEM_DELETE(opts);
	*CheckUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_find_and_modify_new(lua_State* L) {
	auto* opts = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoFindAndModifyOpts);
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoFindAndModifyOpts>(L, kMetaName);
	*ud = opts;
	return 1;
}

int l_find_and_modify_destroy(lua_State* L) {
	l_find_and_modify_gc(L);
	return 0;
}

int l_find_and_modify_set_sort(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	lua_pushboolean(L, opts && doc && opts->SetSort(*doc));
	return 1;
}

int l_find_and_modify_get_sort(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	opts->GetSort(*doc);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_find_and_modify_set_update(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	lua_pushboolean(L, opts && doc && opts->SetUpdate(*doc));
	return 1;
}

int l_find_and_modify_get_update(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	opts->GetUpdate(*doc);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_find_and_modify_set_fields(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	lua_pushboolean(L, opts && doc && opts->SetFields(*doc));
	return 1;
}

int l_find_and_modify_get_fields(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	opts->GetFields(*doc);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_find_and_modify_set_flags(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto flags = CheckIntegerArg<uint32_t>(L, 2);
	lua_pushboolean(L, opts && opts->SetFlags(flags));
	return 1;
}

int l_find_and_modify_get_flags(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	lua_pushinteger(L, opts ? opts->GetFlags() : 0);
	return 1;
}

int l_find_and_modify_set_bypass_document_validation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	lua_pushboolean(L, opts && opts->SetBypassDocumentValidation(lua_toboolean(L, 2) != 0));
	return 1;
}

int l_find_and_modify_get_bypass_document_validation(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	lua_pushboolean(L, opts && opts->GetBypassDocumentValidation());
	return 1;
}

int l_find_and_modify_set_max_time_ms(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto val = CheckIntegerArg<uint32_t>(L, 2);
	lua_pushboolean(L, opts && opts->SetMaxTimeMs(val));
	return 1;
}

int l_find_and_modify_get_max_time_ms(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	lua_pushinteger(L, opts ? opts->GetMaxTimeMs() : 0);
	return 1;
}

int l_find_and_modify_append(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	auto* doc = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	lua_pushboolean(L, opts && doc && opts->Append(*doc));
	return 1;
}

int l_find_and_modify_get_extra(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoFindAndModifyOpts>(L, 1, kMetaName);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::BsonDocument);
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	opts->GetExtra(*doc);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

const luaL_Reg kLib[] = {
	{"find_and_modify_opts_new", l_find_and_modify_new},
	{"find_and_modify_opts_destroy", l_find_and_modify_destroy},
	{"find_and_modify_opts_set_sort", l_find_and_modify_set_sort},
	{"find_and_modify_opts_get_sort", l_find_and_modify_get_sort},
	{"find_and_modify_opts_set_update", l_find_and_modify_set_update},
	{"find_and_modify_opts_get_update", l_find_and_modify_get_update},
	{"find_and_modify_opts_set_fields", l_find_and_modify_set_fields},
	{"find_and_modify_opts_get_fields", l_find_and_modify_get_fields},
	{"find_and_modify_opts_set_flags", l_find_and_modify_set_flags},
	{"find_and_modify_opts_get_flags", l_find_and_modify_get_flags},
	{"find_and_modify_opts_set_bypass_document_validation",
	 l_find_and_modify_set_bypass_document_validation},
	{"find_and_modify_opts_get_bypass_document_validation",
	 l_find_and_modify_get_bypass_document_validation},
	{"find_and_modify_opts_set_max_time_ms", l_find_and_modify_set_max_time_ms},
	{"find_and_modify_opts_get_max_time_ms", l_find_and_modify_get_max_time_ms},
	{"find_and_modify_opts_append", l_find_and_modify_append},
	{"find_and_modify_opts_get_extra", l_find_and_modify_get_extra},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoFindAndModifyOptsMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_find_and_modify_gc);
}

const luaL_Reg* GetMongoFindAndModifyOptsLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
