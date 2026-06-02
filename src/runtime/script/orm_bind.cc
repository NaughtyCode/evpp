#include "runtime/script/orm_bind.h"

#include <cstdint>
#include <limits>

#include "runtime/core/log/log.h"
#include "runtime/database/orm.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

void ReadFindOptionInt32(lua_State* L, int table_index, const char* field_name, int32_t& out) {
	lua_getfield(L, table_index, field_name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		luaL_error(L, "orm find option '%s' must be an integer", field_name);
		return;
	}

	const lua_Integer value = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (value < 0) {
		out = 0;
		return;
	}
	if (value > static_cast<lua_Integer>((std::numeric_limits<int32_t>::max)())) {
		luaL_error(L, "orm find option '%s' is too large", field_name);
		return;
	}
	out = static_cast<int32_t>(value);
}

// orm.define(collection_name, schema_table)
int l_orm_define(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	database::CollectionSchema schema;
	schema.collection_name = name;

	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "fields");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				database::FieldDef field;
				// key at -2 (field name), value at -1 (type string or table)
				if (lua_isstring(L, -2)) {
					field.name = lua_tostring(L, -2);
				}
				if (lua_isstring(L, -1)) {
					std::string type_str = lua_tostring(L, -1);
					if (type_str == "int") field.type = database::FieldType::kInt;
					else if (type_str == "double") field.type = database::FieldType::kDouble;
					else if (type_str == "bool") field.type = database::FieldType::kBool;
					else field.type = database::FieldType::kString;
				}
				schema.fields.push_back(field);
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "indexes");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				database::IndexDef idx;
				if (lua_istable(L, -1)) {
					lua_getfield(L, -1, "fields");
					if (lua_istable(L, -1)) {
						lua_pushnil(L);
						while (lua_next(L, -2) != 0) {
							if (lua_isstring(L, -1)) {
								idx.fields.push_back(lua_tostring(L, -1));
							}
							lua_pop(L, 1);
						}
					}
					lua_pop(L, 1);

					lua_getfield(L, -1, "unique");
					idx.unique = lua_toboolean(L, -1);
					lua_pop(L, 1);
				}
				schema.indexes.push_back(idx);
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}

	database::OrmSession::Instance().RegisterSchema(schema);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ORM: registered schema [{}] with [{}] fields",
					name, schema.fields.size());

	lua_pushboolean(L, 1);
	return 1;
}

// orm.find(collection, query_table) → array of JSON documents
int l_orm_find(lua_State* L) {
	const char* collection = luaL_checkstring(L, 1);
	database::Query query;
	database::FindOptions options;

	if (lua_istable(L, 2)) {
		lua_pushnil(L);
		while (lua_next(L, 2) != 0) {
			if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
				query[lua_tostring(L, -2)] = lua_tostring(L, -1);
			}
			lua_pop(L, 1);
		}
	}

	if (lua_istable(L, 3)) {
		ReadFindOptionInt32(L, 3, "limit", options.limit);
		ReadFindOptionInt32(L, 3, "skip", options.skip);
	}

	auto results = database::OrmSession::Instance().Find(collection, query, options);

	lua_newtable(L);
	for (size_t i = 0; i < results.size(); ++i) {
		lua_pushstring(L, results[i].c_str());
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	return 1;
}

// orm.find_by_id(collection, id) → JSON document or nil
int l_orm_find_by_id(lua_State* L) {
	const char* collection = luaL_checkstring(L, 1);
	const char* id = luaL_checkstring(L, 2);

	auto result = database::OrmSession::Instance().FindById(collection, id);
	if (result) {
		lua_pushstring(L, result->c_str());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// orm.insert(collection, doc_json)
int l_orm_insert(lua_State* L) {
	const char* collection = luaL_checkstring(L, 1);
	const char* doc_json = luaL_checkstring(L, 2);

	bool ok = database::OrmSession::Instance().Insert(collection, doc_json);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// orm.update(collection, id, update_json)
int l_orm_update(lua_State* L) {
	const char* collection = luaL_checkstring(L, 1);
	const char* id = luaL_checkstring(L, 2);
	const char* update_json = luaL_checkstring(L, 3);

	bool ok = database::OrmSession::Instance().Update(collection, id, update_json);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// orm.delete(collection, id)
int l_orm_delete(lua_State* L) {
	const char* collection = luaL_checkstring(L, 1);
	const char* id = luaL_checkstring(L, 2);

	bool ok = database::OrmSession::Instance().DeleteById(collection, id);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// orm.cache_stats() → {hits=N, misses=N, hit_rate=F}
int l_orm_cache_stats(lua_State* L) {
	auto& session = database::OrmSession::Instance();

	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(session.TotalCacheHits()));
	lua_setfield(L, -2, "hits");
	lua_pushinteger(L, static_cast<lua_Integer>(session.TotalCacheMisses()));
	lua_setfield(L, -2, "misses");
	lua_pushnumber(L, session.GlobalHitRate());
	lua_setfield(L, -2, "hit_rate");
	return 1;
}

int l_orm_clear_cache(lua_State* L) {
	database::OrmSession::Instance().ClearAllCaches();
	lua_pushboolean(L, 1);
	return 1;
}

int l_orm_set_database(lua_State* L) {
	const char* database = luaL_checkstring(L, 1);
	database::OrmSession::Instance().SetDefaultDatabase(database);
	lua_pushboolean(L, 1);
	return 1;
}

int l_orm_get_database(lua_State* L) {
	auto database = database::OrmSession::Instance().GetDefaultDatabase();
	lua_pushstring(L, database.c_str());
	return 1;
}

static const luaL_Reg kOrmFuncs[] = {
	{"define",       l_orm_define},
	{"find",         l_orm_find},
	{"find_by_id",   l_orm_find_by_id},
	{"insert",       l_orm_insert},
	{"update",       l_orm_update},
	{"delete",       l_orm_delete},
	{"cache_stats",  l_orm_cache_stats},
	{"clear_cache",  l_orm_clear_cache},
	{"set_database", l_orm_set_database},
	{"get_database", l_orm_get_database},
	{nullptr, nullptr}
};

}  // namespace

void ExportOrm(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);
	for (const luaL_Reg* r = kOrmFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "orm");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ORM API exported to Lua");
}

}  // namespace script
}  // namespace engine
