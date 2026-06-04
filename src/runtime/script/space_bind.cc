#include "runtime/script/space_bind.h"

#include <limits>
#include <string>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/space/space_manager.h"
#include "runtime/space/space_message.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

constexpr size_t kMaxSpacePayloadBytes = 1024 * 1024;
char kCurrentSpaceRegistryKey;

space::Space* GetCurrentSpace(lua_State* L) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, &kCurrentSpaceRegistryKey);
	auto* current = static_cast<space::Space*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return current;
}

void SetCurrentSpace(lua_State* L, space::Space* current_space) {
	if (current_space) {
		lua_pushlightuserdata(L, current_space);
	} else {
		lua_pushnil(L);
	}
	lua_rawsetp(L, LUA_REGISTRYINDEX, &kCurrentSpaceRegistryKey);
}

uint64_t CheckPositiveId(lua_State* L, int index, const char* name) {
	const lua_Integer value = luaL_checkinteger(L, index);
	if (value <= 0) {
		luaL_argerror(L, index, name);
		return 0;
	}
	return static_cast<uint64_t>(value);
}

void PushSpaceInfo(lua_State* L, const space::Space& sp) {
	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(sp.GetId()));
	lua_setfield(L, -2, "id");
	lua_pushstring(L, sp.GetName().c_str());
	lua_setfield(L, -2, "name");
	lua_pushinteger(L, static_cast<lua_Integer>(sp.EntityCount()));
	lua_setfield(L, -2, "entity_count");
	lua_pushinteger(L, static_cast<lua_Integer>(sp.PlayerCount()));
	lua_setfield(L, -2, "player_count");
}

bool ReadSizeField(lua_State* L,
				   int table_index,
				   const char* field_name,
				   size_t& out,
				   bool allow_zero,
				   std::string& error) {
	lua_getfield(L, table_index, field_name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return true;
	}

	if (!lua_isinteger(L, -1)) {
		error = "space config field '" + std::string(field_name) + "' must be an integer";
		lua_pop(L, 1);
		return false;
	}

	lua_Integer value = lua_tointeger(L, -1);
	lua_pop(L, 1);

	if (value < 0 || (!allow_zero && value == 0)) {
		error = "space config field '" + std::string(field_name) + "' must be " +
				(allow_zero ? "non-negative" : "positive");
		return false;
	}

	auto unsigned_value = static_cast<unsigned long long>(value);
	if (unsigned_value > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
		error = "space config field '" + std::string(field_name) + "' is too large";
		return false;
	}

	out = static_cast<size_t>(value);
	return true;
}

bool ReadScriptsField(lua_State* L,
					  int table_index,
					  std::vector<std::string>& scripts,
					  std::string& error) {
	lua_getfield(L, table_index, "scripts");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return true;
	}

	if (!lua_istable(L, -1)) {
		error = "space config field 'scripts' must be an array table";
		lua_pop(L, 1);
		return false;
	}

	const size_t count = lua_rawlen(L, -1);
	for (size_t i = 1; i <= count; ++i) {
		lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
		if (!lua_isstring(L, -1)) {
			error = "space config scripts[" + std::to_string(i) + "] must be a string";
			lua_pop(L, 2);
			return false;
		}
		size_t len = 0;
		const char* script = lua_tolstring(L, -1, &len);
		scripts.emplace_back(script ? script : "", len);
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
	return true;
}

// space.create(name, config)
// config: { max_entities = N, max_players = N, scripts = {...} }
int l_space_create(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	bool parse_failed = false;
	int result_count = 0;

	{
		space::SpaceConfig config;
		config.name = name;
		std::string error;

		if (!lua_isnoneornil(L, 2)) {
			if (!lua_istable(L, 2)) {
				error = "space config must be a table";
			} else {
				int cfg_index = lua_absindex(L, 2);
				if (!ReadSizeField(L, cfg_index, "max_entities", config.max_entities, false, error) ||
					!ReadSizeField(L, cfg_index, "max_players", config.max_players, true, error) ||
					!ReadScriptsField(L, cfg_index, config.entry_scripts, error)) {
					// error filled by helper
				}
			}
		}

		if (!error.empty()) {
			lua_pushlstring(L, error.data(), error.size());
			parse_failed = true;
		} else {
			auto& manager = space::SpaceManager::Instance();
			auto* sp = manager.CreateSpace(config);
			if (!sp) {
				lua_pushnil(L);
				lua_pushstring(L, "failed to create space");
				result_count = 2;
			} else if (!config.entry_scripts.empty()) {
				if (!sp->LoadScripts(config.entry_scripts, &error)) {
					space::SpaceId id = sp->GetId();
					manager.DestroySpace(id);
					lua_pushnil(L);
					lua_pushlstring(L, error.data(), error.size());
					result_count = 2;
				} else {
					lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
					result_count = 1;
				}
			} else {
				lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
				result_count = 1;
			}
		}
	}

	if (parse_failed) {
		return lua_error(L);
	}
	return result_count;
}

// space.get(id) -> space info table or nil
int l_space_get(lua_State* L) {
	space::SpaceId id =
		static_cast<space::SpaceId>(CheckPositiveId(L, 1, "space id must be positive"));
	auto* sp = space::SpaceManager::Instance().GetSpace(id);
	if (!sp) {
		lua_pushnil(L);
		return 1;
	}

	PushSpaceInfo(L, *sp);
	return 1;
}

// space.destroy(id)
int l_space_destroy(lua_State* L) {
	space::SpaceId id =
		static_cast<space::SpaceId>(CheckPositiveId(L, 1, "space id must be positive"));
	auto* current = GetCurrentSpace(L);
	if (current && current->GetId() == id) {
		lua_pushnil(L);
		lua_pushstring(L, "cannot destroy the current space from its own script");
		return 2;
	}

	space::SpaceManager::Instance().DestroySpace(id);
	return 0;
}

// space.send(space_id, target_entity, payload[, source_entity])
int l_space_send(lua_State* L) {
	space::SpaceId target =
		static_cast<space::SpaceId>(CheckPositiveId(L, 1, "space id must be positive"));
	entity::EntityId target_entity = static_cast<entity::EntityId>(
		CheckPositiveId(L, 2, "target entity id must be positive"));
	size_t len = 0;
	const char* payload = luaL_checklstring(L, 3, &len);

	if (len > kMaxSpacePayloadBytes) {
		{
			std::string message = "space payload exceeds maximum size (" +
								  std::to_string(len) + " > " +
								  std::to_string(kMaxSpacePayloadBytes) + ")";
			lua_pushlstring(L, message.data(), message.size());
		}
		return lua_error(L);
	}

	space::SpaceMessage msg;
	msg.target_space = target;
	msg.target_entity = target_entity;
	msg.payload.assign(payload, len);

	if (auto* current = GetCurrentSpace(L)) {
		msg.source_space = current->GetId();
	}
	if (!lua_isnoneornil(L, 4)) {
		msg.source_entity = static_cast<entity::EntityId>(
			CheckPositiveId(L, 4, "source entity id must be positive"));
	}

	space::SpaceMessageRouter::Instance().SendMessage(std::move(msg));
	return 0;
}

// space.list() -> array of space info tables
int l_space_list(lua_State* L) {
	lua_newtable(L);
	int idx = 1;
	space::SpaceManager::Instance().ForEachSpace([&](space::Space& sp) {
		PushSpaceInfo(L, sp);
		lua_rawseti(L, -2, idx++);
	});
	return 1;
}

// space.current() -> info for this VM's bound space, or the default space.
int l_space_current(lua_State* L) {
	auto* sp = GetCurrentSpace(L);
	if (!sp) {
		sp = space::SpaceManager::Instance().GetDefaultSpace();
	}
	if (!sp) {
		lua_pushnil(L);
		return 1;
	}

	PushSpaceInfo(L, *sp);
	return 1;
}

// Internal: space._deliver_message(src_space, src_entity, tgt_entity, payload)
// Called by SpaceMessageRouter to push a cross-space message into Lua.
int l_space_deliver_message(lua_State* L) {
	lua_getglobal(L, "space");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	lua_getfield(L, -1, "_pending_messages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, "_pending_messages");
	}
	lua_remove(L, -2);  // keep pending table, remove space table

	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "source_space");
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "source_entity");
	lua_pushvalue(L, 3);
	lua_setfield(L, -2, "target_entity");
	lua_pushvalue(L, 4);
	lua_setfield(L, -2, "payload");

	lua_Integer next_index = luaL_len(L, -2) + 1;
	lua_rawseti(L, -2, next_index);
	lua_pop(L, 1);  // pending table
	return 0;
}

// space.poll() -> next pending message or nil
int l_space_poll(lua_State* L) {
	lua_getglobal(L, "space");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	lua_getfield(L, -1, "_pending_messages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		lua_pushnil(L);
		return 1;
	}

	lua_Integer len = luaL_len(L, -1);
	if (len == 0) {
		lua_pop(L, 2);
		lua_pushnil(L);
		return 1;
	}

	lua_rawgeti(L, -1, 1);
	for (lua_Integer i = 1; i < len; ++i) {
		lua_rawgeti(L, -2, i + 1);
		lua_rawseti(L, -3, i);
	}
	lua_pushnil(L);
	lua_rawseti(L, -3, len);

	lua_remove(L, -2);  // pending table
	lua_remove(L, -2);  // space table
	return 1;
}

// _on_connection_data(conn_lightuserdata, data_string)
// Default no-op; game scripts override this in the space table.
int l_space_on_connection_data(lua_State* L) {
	return 0;
}

static const luaL_Reg kSpaceFuncs[] = {
	{"create",              l_space_create},
	{"get",                 l_space_get},
	{"destroy",             l_space_destroy},
	{"send",                l_space_send},
	{"list",                l_space_list},
	{"current",             l_space_current},
	{"poll",                l_space_poll},
	{"_deliver_message",    l_space_deliver_message},
	{"_on_connection_data", l_space_on_connection_data},
	{nullptr, nullptr}
};

}  // namespace

void ExportSpace(ScriptVM& vm, space::Space* current_space) {
	auto* L = vm.GetState();
	if (!L) return;

	SetCurrentSpace(L, current_space);

	lua_getglobal(L, "space");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
	}

	for (const luaL_Reg* r = kSpaceFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}

	lua_getfield(L, -1, "_pending_messages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setfield(L, -2, "_pending_messages");
	} else {
		lua_pop(L, 1);
	}

	lua_setglobal(L, "space");

	auto* logger = GetLogger();
	if (logger) {
		ENGINE_LOG_INFO(logger, "Space API exported to Lua");
	}
}

void ClearCurrentSpace(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;
	SetCurrentSpace(L, nullptr);
}

}  // namespace script
}  // namespace engine
