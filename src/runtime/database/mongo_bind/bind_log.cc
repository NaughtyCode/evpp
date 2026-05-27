#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo_bind/bind_log.h"

#include <new>

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/database/mongo/mongo_log.h"
#include "runtime/database/mongo_bind/bind_util.h"

namespace engine {
namespace script {
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// MongoStructuredLogOpts
// ═══════════════════════════════════════════════════════════════════════════

const char* kOptsMeta = "mongoc.structured_log_opts";

int l_log_opts_gc(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	delete opts;
	*CheckUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta) = nullptr;
	return 0;
}

int l_log_opts_new(lua_State* L) {
	auto* opts = new (std::nothrow) mongo::MongoStructuredLogOpts();
	if (!opts) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoStructuredLogOpts>(L, kOptsMeta);
	*ud = opts;
	return 1;
}

int l_log_opts_destroy(lua_State* L) {
	l_log_opts_gc(L);
	return 0;
}

int l_log_opts_set_max_level_for_component(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	auto component = static_cast<mongo::MongoStructuredLogComponent>(luaL_checkinteger(L, 2));
	auto level = static_cast<mongo::MongoStructuredLogLevel>(luaL_checkinteger(L, 3));
	lua_pushboolean(L, opts && opts->SetMaxLevelForComponent(component, level));
	return 1;
}

int l_log_opts_set_max_level_for_all_components(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	auto level = static_cast<mongo::MongoStructuredLogLevel>(luaL_checkinteger(L, 2));
	lua_pushboolean(L, opts && opts->SetMaxLevelForAllComponents(level));
	return 1;
}

int l_log_opts_set_max_levels_from_env(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	lua_pushboolean(L, opts && opts->SetMaxLevelsFromEnv());
	return 1;
}

int l_log_opts_get_max_level_for_component(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	auto component = static_cast<mongo::MongoStructuredLogComponent>(luaL_checkinteger(L, 2));
	lua_pushinteger(L,
					opts ? static_cast<lua_Integer>(opts->GetMaxLevelForComponent(component)) : 0);
	return 1;
}

int l_log_opts_get_max_document_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	lua_pushinteger(L, opts ? static_cast<lua_Integer>(opts->GetMaxDocumentLength()) : 0);
	return 1;
}

int l_log_opts_set_max_document_length(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	size_t len = static_cast<size_t>(luaL_checkinteger(L, 2));
	lua_pushboolean(L, opts && opts->SetMaxDocumentLength(len));
	return 1;
}

int l_log_opts_set_max_document_length_from_env(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	lua_pushboolean(L, opts && opts->SetMaxDocumentLengthFromEnv());
	return 1;
}

int l_log_opts_get_level_name(lua_State* L) {
	auto level = static_cast<mongo::MongoStructuredLogLevel>(luaL_checkinteger(L, 1));
	const char* name = mongo::MongoStructuredLogOpts::GetLevelName(level);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

int l_log_opts_get_named_level(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	mongo::MongoStructuredLogLevel level;
	if (mongo::MongoStructuredLogOpts::GetNamedLevel(name, &level)) {
		lua_pushinteger(L, static_cast<lua_Integer>(level));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_log_opts_get_component_name(lua_State* L) {
	auto component = static_cast<mongo::MongoStructuredLogComponent>(luaL_checkinteger(L, 1));
	const char* name = mongo::MongoStructuredLogOpts::GetComponentName(component);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

int l_log_opts_get_named_component(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	mongo::MongoStructuredLogComponent component;
	if (mongo::MongoStructuredLogOpts::GetNamedComponent(name, &component)) {
		lua_pushinteger(L, static_cast<lua_Integer>(component));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_log_opts_get_raw(lua_State* L) {
	auto* opts = GetUserdata<mongo::MongoStructuredLogOpts>(L, 1, kOptsMeta);
	if (!opts) {
		lua_pushnil(L);
		return 1;
	}
	void* raw = opts->Raw();
	if (raw)
		lua_pushlightuserdata(L, raw);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kOptsLib[] = {
	{"log_opts_new", l_log_opts_new},
	{"log_opts_destroy", l_log_opts_destroy},
	{"log_opts_set_max_level_for_component", l_log_opts_set_max_level_for_component},
	{"log_opts_set_max_level_for_all_components", l_log_opts_set_max_level_for_all_components},
	{"log_opts_set_max_levels_from_env", l_log_opts_set_max_levels_from_env},
	{"log_opts_get_max_level_for_component", l_log_opts_get_max_level_for_component},
	{"log_opts_get_max_document_length", l_log_opts_get_max_document_length},
	{"log_opts_set_max_document_length", l_log_opts_set_max_document_length},
	{"log_opts_set_max_document_length_from_env", l_log_opts_set_max_document_length_from_env},
	{"log_opts_get_level_name", l_log_opts_get_level_name},
	{"log_opts_get_named_level", l_log_opts_get_named_level},
	{"log_opts_get_component_name", l_log_opts_get_component_name},
	{"log_opts_get_named_component", l_log_opts_get_named_component},
	{"log_opts_get_raw", l_log_opts_get_raw},
	{nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoStructuredLogEntry
// ═══════════════════════════════════════════════════════════════════════════

const char* kEntryMeta = "mongoc.structured_log_entry";

int l_log_entry_gc(lua_State* L) {
	auto* e = GetUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta);
	delete e;
	*CheckUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta) = nullptr;
	return 0;
}

int l_log_entry_new(lua_State* L) {
	void* raw = lua_touserdata(L, 1);
	if (!raw) {
		lua_pushnil(L);
		lua_pushstring(L, "invalid raw entry pointer");
		return 2;
	}
	auto* e = new (std::nothrow) mongo::MongoStructuredLogEntry(raw);
	if (!e) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	auto** ud = NewUserdata<mongo::MongoStructuredLogEntry>(L, kEntryMeta);
	*ud = e;
	return 1;
}

int l_log_entry_destroy(lua_State* L) {
	l_log_entry_gc(L);
	return 0;
}

int l_log_entry_message_as_bson(lua_State* L) {
	auto* e = GetUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta);
	if (!e) {
		lua_pushnil(L);
		return 1;
	}
	auto* doc = new (std::nothrow) mongo::BsonDocument();
	if (!doc) {
		lua_pushnil(L);
		lua_pushstring(L, "allocation failure");
		return 2;
	}
	e->MessageAsBson(doc);
	auto** ud = NewUserdata<mongo::BsonDocument>(L, "bson.doc");
	*ud = doc;
	return 1;
}

int l_log_entry_get_level(lua_State* L) {
	auto* e = GetUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta);
	lua_pushinteger(L, e ? static_cast<lua_Integer>(e->GetLevel()) : 0);
	return 1;
}

int l_log_entry_get_component(lua_State* L) {
	auto* e = GetUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta);
	lua_pushinteger(L, e ? static_cast<lua_Integer>(e->GetComponent()) : 0);
	return 1;
}

int l_log_entry_get_message_string(lua_State* L) {
	auto* e = GetUserdata<mongo::MongoStructuredLogEntry>(L, 1, kEntryMeta);
	if (!e) {
		lua_pushnil(L);
		return 1;
	}
	const char* msg = e->GetMessageString();
	if (msg)
		lua_pushstring(L, msg);
	else
		lua_pushnil(L);
	return 1;
}

const luaL_Reg kEntryLib[] = {
	{"log_entry_new", l_log_entry_new},
	{"log_entry_destroy", l_log_entry_destroy},
	{"log_entry_message_as_bson", l_log_entry_message_as_bson},
	{"log_entry_get_level", l_log_entry_get_level},
	{"log_entry_get_component", l_log_entry_get_component},
	{"log_entry_get_message_string", l_log_entry_get_message_string},
	{nullptr, nullptr},
};

// ═══════════════════════════════════════════════════════════════════════════
// MongoLog static methods
// ═══════════════════════════════════════════════════════════════════════════

int l_log_level_to_string(lua_State* L) {
	auto level = static_cast<mongo::MongoLogLevel>(luaL_checkinteger(L, 1));
	const char* name = mongo::MongoLog::LevelToString(level);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

int l_log_trace_enable(lua_State* L) {
	mongo::MongoLog::TraceEnable();
	return 0;
}

int l_log_trace_disable(lua_State* L) {
	mongo::MongoLog::TraceDisable();
	return 0;
}

int l_log_default_handler(lua_State* L) {
	auto level = static_cast<mongo::MongoLogLevel>(luaL_checkinteger(L, 1));
	const char* domain = luaL_checkstring(L, 2);
	const char* message = luaL_checkstring(L, 3);
	mongo::MongoLog::DefaultHandler(level, domain, message);
	return 0;
}

const luaL_Reg kLogLib[] = {
	{"log_level_to_string", l_log_level_to_string},
	{"log_trace_enable", l_log_trace_enable},
	{"log_trace_disable", l_log_trace_disable},
	{"log_default_handler", l_log_default_handler},
	{nullptr, nullptr},
};

}  // namespace

// ── Metatable registration functions ───────────────────────────────────────

void RegisterMongoStructuredLogOptsMeta(lua_State* L) {
	RegisterMetatable(L, kOptsMeta, nullptr, l_log_opts_gc);
}

void RegisterMongoStructuredLogEntryMeta(lua_State* L) {
	RegisterMetatable(L, kEntryMeta, nullptr, l_log_entry_gc);
}

// ── Lib getter functions ───────────────────────────────────────────────────

const luaL_Reg* GetMongoStructuredLogOptsLib() {
	return kOptsLib;
}
const luaL_Reg* GetMongoStructuredLogEntryLib() {
	return kEntryLib;
}
const luaL_Reg* GetMongoLogLib() {
	return kLogLib;
}

}  // namespace script
}  // namespace engine

#endif
