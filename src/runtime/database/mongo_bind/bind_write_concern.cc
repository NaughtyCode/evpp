#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_write_concern.h"

#include <new>

#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

const char* kMetaName = "mongoc.write_concern";

int l_write_concern_gc(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	MEM_DELETE(concern);
	*CheckUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName) = nullptr;
	return 0;
}

int l_write_concern_new(lua_State* L) {
	auto* concern = MEM_NEW_NOTHROW(mongo::MongoWriteConcern);
	if (!concern) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoWriteConcern>(L, kMetaName);
	*ud = concern;
	return 1;
}

int l_write_concern_destroy(lua_State* L) {
	l_write_concern_gc(L);
	return 0;
}

int l_write_concern_get_w(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushinteger(L, concern ? concern->GetW() : 0);
	return 1;
}

int l_write_concern_set_w(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	auto w = static_cast<int32_t>(luaL_checkinteger(L, 2));
	if (concern) concern->SetW(w);
	return 0;
}

int l_write_concern_get_journal(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->GetJournal());
	return 1;
}

int l_write_concern_set_journal(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	bool journal = lua_toboolean(L, 2) != 0;
	if (concern) concern->SetJournal(journal);
	return 0;
}

int l_write_concern_get_w_timeout(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushinteger(L, concern ? concern->GetWTimeout() : 0);
	return 1;
}

int l_write_concern_set_w_timeout(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	auto timeout = static_cast<int32_t>(luaL_checkinteger(L, 2));
	if (concern) concern->SetWTimeout(timeout);
	return 0;
}

int l_write_concern_is_acknowledged(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->IsAcknowledged());
	return 1;
}

int l_write_concern_is_valid(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->IsValid());
	return 1;
}

int l_write_concern_is_default(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->IsDefault());
	return 1;
}

int l_write_concern_copy(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	if (!concern) {
		lua_pushnil(L);
		return 1;
	}
	auto* copy = MEM_NEW_NOTHROW(mongo::MongoWriteConcern, concern->Copy());
	if (!copy) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoWriteConcern>(L, kMetaName);
	*ud = copy;
	return 1;
}

int l_write_concern_journal_is_set(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->JournalIsSet());
	return 1;
}

int l_write_concern_get_w_timeout_int64(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushinteger(L, concern ? concern->GetWTimeoutInt64() : 0);
	return 1;
}

int l_write_concern_set_w_timeout_int64(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	auto timeout = static_cast<int64_t>(luaL_checkinteger(L, 2));
	if (concern) concern->SetWTimeoutInt64(timeout);
	return 0;
}

int l_write_concern_get_w_majority(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	lua_pushboolean(L, concern && concern->GetWMajority());
	return 1;
}

int l_write_concern_set_w_majority(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	auto timeout = static_cast<int32_t>(luaL_checkinteger(L, 2));
	if (concern) concern->SetWMajority(timeout);
	return 0;
}

int l_write_concern_get_w_tag(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	const char* tag = concern ? concern->GetWTag() : nullptr;
	if (tag)
		lua_pushstring(L, tag);
	else
		lua_pushnil(L);
	return 1;
}

int l_write_concern_set_w_tag(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	const char* tag = luaL_checkstring(L, 2);
	lua_pushinteger(L, concern ? concern->SetWTag(tag) : 0);
	return 1;
}

int l_write_concern_append_to_opts(lua_State* L) {
	auto* concern = GetUserdata<mongo::MongoWriteConcern>(L, 1, kMetaName);
	auto* opts = GetUserdata<mongo::BsonDocument>(L, 2, "bson.doc");
	if (!concern || !opts) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L, concern->AppendToOpts(*opts));
	return 1;
}

const luaL_Reg kLib[] = {
	{"write_concern_new", l_write_concern_new},
	{"write_concern_destroy", l_write_concern_destroy},
	{"write_concern_get_w", l_write_concern_get_w},
	{"write_concern_set_w", l_write_concern_set_w},
	{"write_concern_get_journal", l_write_concern_get_journal},
	{"write_concern_set_journal", l_write_concern_set_journal},
	{"write_concern_get_w_timeout", l_write_concern_get_w_timeout},
	{"write_concern_set_w_timeout", l_write_concern_set_w_timeout},
	{"write_concern_is_acknowledged", l_write_concern_is_acknowledged},
	{"write_concern_is_valid", l_write_concern_is_valid},
	{"write_concern_is_default", l_write_concern_is_default},
	{"write_concern_copy", l_write_concern_copy},
	{"write_concern_journal_is_set", l_write_concern_journal_is_set},
	{"write_concern_get_w_timeout_int64", l_write_concern_get_w_timeout_int64},
	{"write_concern_set_w_timeout_int64", l_write_concern_set_w_timeout_int64},
	{"write_concern_get_w_majority", l_write_concern_get_w_majority},
	{"write_concern_set_w_majority", l_write_concern_set_w_majority},
	{"write_concern_get_w_tag", l_write_concern_get_w_tag},
	{"write_concern_set_w_tag", l_write_concern_set_w_tag},
	{"write_concern_append_to_opts", l_write_concern_append_to_opts},
	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoWriteConcernMeta(lua_State* L) {
	RegisterMetatable(L, kMetaName, nullptr, l_write_concern_gc);
}

const luaL_Reg* GetMongoWriteConcernLib() {
	return kLib;
}

}  // namespace script
}  // namespace engine

#endif
