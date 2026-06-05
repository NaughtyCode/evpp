#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/bind/bind_misc.h"

#include <cstring>
#include <new>

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_flags.h"
#include "runtime/database/mongo/mongo_handshake.h"
#include "runtime/database/mongo/mongo_init.h"
#include "runtime/database/mongo/mongo_iovec.h"
#include "runtime/database/mongo/mongo_optional.h"
#include "runtime/database/mongo/mongo_rand.h"
#include "runtime/database/mongo/mongo_system.h"
#include "runtime/database/mongo/mongo_version.h"
#include "runtime/database/mongo/bind/bind_util.h"

namespace engine {
namespace script {
namespace {

// Part 1: MongoOptional  - proper class with metatable

const char* kOptionalMeta = "mongoc.optional";

int l_optional_gc(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	CLOUDENGINE_MEM_DELETE(opt);
	*CheckUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta) = nullptr;
	return 0;
}

int l_optional_new(lua_State* L) {
	auto* opt = CLOUDENGINE_MEM_NEW_NOTHROW(mongo::MongoOptional);
	if (!opt) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		lua_pushnil(L);
		return 3;
	}
	auto** ud = NewUserdata<mongo::MongoOptional>(L, kOptionalMeta);
	*ud = opt;
	return 1;
}

int l_optional_destroy(lua_State* L) {
	l_optional_gc(L);
	return 0;
}

int l_optional_init(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	if (opt) opt->Init();
	return 0;
}

int l_optional_is_set(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	lua_pushboolean(L, opt && opt->IsSet());
	return 1;
}

int l_optional_value(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	lua_pushboolean(L, opt && opt->Value());
	return 1;
}

int l_optional_set_value(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	if (opt) opt->SetValue(lua_toboolean(L, 2) != 0);
	return 0;
}

int l_optional_copy(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	auto* other = GetUserdata<mongo::MongoOptional>(L, 2, kOptionalMeta);
	if (opt && other) opt->Copy(*other);
	return 0;
}

int l_optional_get_raw(lua_State* L) {
	auto* opt = GetUserdata<mongo::MongoOptional>(L, 1, kOptionalMeta);
	if (!opt) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opt->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kOptionalLib[] = {
	{"optional_new", l_optional_new},
	{"optional_destroy", l_optional_destroy},
	{"optional_init", l_optional_init},
	{"optional_is_set", l_optional_is_set},
	{"optional_value", l_optional_value},
	{"optional_set_value", l_optional_set_value},
	{"optional_copy", l_optional_copy},
	{"optional_get_raw", l_optional_get_raw},
	{nullptr, nullptr},
};

// Part 2: Static utility module  - mongo_flags.h

int l_insert_flags_none(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoInsertFlags::kNone));
	return 1;
}

int l_insert_flags_continue_on_error(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoInsertFlags::kContinueOnError));
	return 1;
}

int l_insert_flags_no_validate(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoInsertFlags::kNoValidate));
	return 1;
}

int l_update_flags_none(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoUpdateFlags::kNone));
	return 1;
}

int l_update_flags_upsert(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoUpdateFlags::kUpsert));
	return 1;
}

int l_update_flags_multi_update(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoUpdateFlags::kMultiUpdate));
	return 1;
}

int l_update_flags_no_validate(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoUpdateFlags::kNoValidate));
	return 1;
}

int l_remove_flags_none(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoRemoveFlags::kNone));
	return 1;
}

int l_remove_flags_single_remove(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoRemoveFlags::kSingleRemove));
	return 1;
}

int l_query_flags_none(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kNone));
	return 1;
}

int l_query_flags_tailable_cursor(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kTailableCursor));
	return 1;
}

int l_query_flags_secondary_ok(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kSecondaryOk));
	return 1;
}

int l_query_flags_oplog_replay(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kOplogReplay));
	return 1;
}

int l_query_flags_no_cursor_timeout(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kNoCursorTimeout));
	return 1;
}

int l_query_flags_await_data(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kAwaitData));
	return 1;
}

int l_query_flags_exhaust(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kExhaust));
	return 1;
}

int l_query_flags_partial(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoQueryFlags::kPartial));
	return 1;
}

int l_opcode_reply(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kReply));
	return 1;
}

int l_opcode_update(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kUpdate));
	return 1;
}

int l_opcode_insert(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kInsert));
	return 1;
}

int l_opcode_query(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kQuery));
	return 1;
}

int l_opcode_get_more(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kGetMore));
	return 1;
}

int l_opcode_delete(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kDelete));
	return 1;
}

int l_opcode_kill_cursors(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kKillCursors));
	return 1;
}

int l_opcode_compressed(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kCompressed));
	return 1;
}

int l_opcode_msg(lua_State* L) {
	lua_pushinteger(L, static_cast<lua_Integer>(mongo::MongoOpcode::kMsg));
	return 1;
}

// Part 2 continued: mongo_init.h

int l_mongo_init(lua_State* L) {
	mongo::MongoInit::Init();
	return 0;
}

int l_mongo_cleanup(lua_State* L) {
	mongo::MongoInit::Cleanup();
	return 0;
}

// Part 2 continued: mongo_version.h

int l_mongo_get_major_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetMajorVersion());
	return 1;
}

int l_mongo_get_minor_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetMinorVersion());
	return 1;
}

int l_mongo_get_micro_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetMicroVersion());
	return 1;
}

int l_mongo_get_version(lua_State* L) {
	lua_pushstring(L, mongo::MongoVersion::GetVersion());
	return 1;
}

int l_mongo_check_version(lua_State* L) {
	int major = CheckIntegerArg<int>(L, 1);
	int minor = CheckIntegerArg<int>(L, 2);
	int micro = CheckIntegerArg<int>(L, 3);
	lua_pushboolean(L, mongo::MongoVersion::CheckVersion(major, minor, micro));
	return 1;
}

int l_bson_get_major_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetBsonMajorVersion());
	return 1;
}

int l_bson_get_minor_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetBsonMinorVersion());
	return 1;
}

int l_bson_get_micro_version(lua_State* L) {
	lua_pushinteger(L, mongo::MongoVersion::GetBsonMicroVersion());
	return 1;
}

int l_bson_get_version(lua_State* L) {
	lua_pushstring(L, mongo::MongoVersion::GetBsonVersion());
	return 1;
}

int l_bson_check_version(lua_State* L) {
	int major = CheckIntegerArg<int>(L, 1);
	int minor = CheckIntegerArg<int>(L, 2);
	int micro = CheckIntegerArg<int>(L, 3);
	lua_pushboolean(L, mongo::MongoVersion::CheckBsonVersion(major, minor, micro));
	return 1;
}

// Part 2 continued: mongo_handshake.h

int l_mongo_handshake_data_append(lua_State* L) {
	const char* driver_name = luaL_checkstring(L, 1);
	const char* driver_version = luaL_checkstring(L, 2);
	const char* platform = luaL_checkstring(L, 3);
	lua_pushboolean(L, mongo::MongoHandshake::DataAppend(driver_name, driver_version, platform));
	return 1;
}

// Part 2 continued: mongo_rand.h

int l_mongo_rand_seed(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);
	mongo::MongoRand::Seed(data, static_cast<int>(len));
	return 0;
}

int l_mongo_rand_add(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);
	double entropy = static_cast<double>(luaL_checknumber(L, 2));
	mongo::MongoRand::Add(data, static_cast<int>(len), entropy);
	return 0;
}

int l_mongo_rand_status(lua_State* L) {
	lua_pushinteger(L, mongo::MongoRand::Status());
	return 1;
}

// Part 2 continued: mongo_system.h

int l_mongo_system_initialize(lua_State* L) {
	lua_pushboolean(L, mongo::MongoSystem::Instance().Initialize());
	return 1;
}

int l_mongo_system_shutdown(lua_State* L) {
	mongo::MongoSystem::Instance().Shutdown();
	return 0;
}

int l_mongo_system_is_initialized(lua_State* L) {
	lua_pushboolean(L, mongo::MongoSystem::Instance().IsInitialized());
	return 1;
}

// kMiscLib  - all static utility functions (no metatable, added to module)

const luaL_Reg kMiscLib[] = {
	// mongo_flags.h  - insert flags
	{"insert_flags_none", l_insert_flags_none},
	{"insert_flags_continue_on_error", l_insert_flags_continue_on_error},
	{"insert_flags_no_validate", l_insert_flags_no_validate},

	// mongo_flags.h  - update flags
	{"update_flags_none", l_update_flags_none},
	{"update_flags_upsert", l_update_flags_upsert},
	{"update_flags_multi_update", l_update_flags_multi_update},
	{"update_flags_no_validate", l_update_flags_no_validate},

	// mongo_flags.h  - remove flags
	{"remove_flags_none", l_remove_flags_none},
	{"remove_flags_single_remove", l_remove_flags_single_remove},

	// mongo_flags.h  - query flags
	{"query_flags_none", l_query_flags_none},
	{"query_flags_tailable_cursor", l_query_flags_tailable_cursor},
	{"query_flags_secondary_ok", l_query_flags_secondary_ok},
	{"query_flags_oplog_replay", l_query_flags_oplog_replay},
	{"query_flags_no_cursor_timeout", l_query_flags_no_cursor_timeout},
	{"query_flags_await_data", l_query_flags_await_data},
	{"query_flags_exhaust", l_query_flags_exhaust},
	{"query_flags_partial", l_query_flags_partial},

	// mongo_flags.h  - opcodes
	{"opcode_reply", l_opcode_reply},
	{"opcode_update", l_opcode_update},
	{"opcode_insert", l_opcode_insert},
	{"opcode_query", l_opcode_query},
	{"opcode_get_more", l_opcode_get_more},
	{"opcode_delete", l_opcode_delete},
	{"opcode_kill_cursors", l_opcode_kill_cursors},
	{"opcode_compressed", l_opcode_compressed},
	{"opcode_msg", l_opcode_msg},

	// mongo_init.h
	{"mongo_init", l_mongo_init},
	{"mongo_cleanup", l_mongo_cleanup},

	// mongo_version.h
	{"mongo_get_major_version", l_mongo_get_major_version},
	{"mongo_get_minor_version", l_mongo_get_minor_version},
	{"mongo_get_micro_version", l_mongo_get_micro_version},
	{"mongo_get_version", l_mongo_get_version},
	{"mongo_check_version", l_mongo_check_version},
	{"bson_get_major_version", l_bson_get_major_version},
	{"bson_get_minor_version", l_bson_get_minor_version},
	{"bson_get_micro_version", l_bson_get_micro_version},
	{"bson_get_version", l_bson_get_version},
	{"bson_check_version", l_bson_check_version},

	// mongo_handshake.h
	{"mongo_handshake_data_append", l_mongo_handshake_data_append},

	// mongo_rand.h
	{"mongo_rand_seed", l_mongo_rand_seed},
	{"mongo_rand_add", l_mongo_rand_add},
	{"mongo_rand_status", l_mongo_rand_status},

	// mongo_system.h
	{"mongo_system_initialize", l_mongo_system_initialize},
	{"mongo_system_shutdown", l_mongo_system_shutdown},
	{"mongo_system_is_initialized", l_mongo_system_is_initialized},

	{nullptr, nullptr},
};

}  // namespace

void RegisterMongoOptionalMeta(lua_State* L) {
	RegisterMetatable(L, kOptionalMeta, nullptr, l_optional_gc);
}

const luaL_Reg* GetMongoOptionalLib() {
	return kOptionalLib;
}

const luaL_Reg* GetMongoMiscLib() {
	return kMiscLib;
}

}  // namespace script
}  // namespace engine

#endif
