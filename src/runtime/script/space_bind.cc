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

void ReadSizeField(lua_State* L,
				   int table_index,
				   const char* field_name,
				   size_t& out,
				   bool allow_zero) {
	lua_getfield(L, table_index, field_name);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	if (!lua_isinteger(L, -1)) {
		luaL_error(L, "space config field '%s' must be an integer", field_name);
	}

	lua_Integer value = lua_tointeger(L, -1);
	lua_pop(L, 1);

	if (value < 0 || (!allow_zero && value == 0)) {
		luaL_error(L,
				   "space config field '%s' must be %s",
				   field_name,
				   allow_zero ? "non-negative" : "positive");
	}

	auto unsigned_value = static_cast<unsigned long long>(value);
	if (unsigned_value > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
		luaL_error(L, "space config field '%s' is too large", field_name);
	}

	out = static_cast<size_t>(value);
}

void ReadScriptsField(lua_State* L, int table_index, std::vector<std::string>& scripts) {
	lua_getfield(L, table_index, "scripts");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	if (!lua_istable(L, -1)) {
		luaL_error(L, "space config field 'scripts' must be an array table");
	}

	lua_Integer count = luaL_len(L, -1);
	for (lua_Integer i = 1; i <= count; ++i) {
		lua_rawgeti(L, -1, i);
		if (!lua_isstring(L, -1)) {
			std::string message =
				"space config scripts[" + std::to_string(static_cast<long long>(i)) +
				"] must be a string";
			luaL_error(L, "%s", message.c_str());
		}
		scripts.emplace_back(lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
}

// space.create(name, config)
// config: { max_entities = N, max_players = N, scripts = {...} }
int l_space_create(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	space::SpaceConfig config;
	config.name = name;

	if (!lua_isnoneornil(L, 2)) {
		luaL_checktype(L, 2, LUA_TTABLE);
		int cfg_index = lua_absindex(L, 2);
		ReadSizeField(L, cfg_index, "max_entities", config.max_entities, false);
		ReadSizeField(L, cfg_index, "max_players", config.max_players, true);
		ReadScriptsField(L, cfg_index, config.entry_scripts);
	}

	auto& manager = space::SpaceManager::Instance();
	auto* sp = manager.CreateSpace(config);
	if (!sp) {
		lua_pushnil(L);
		lua_pushstring(L, "failed to create space");
		return 2;
	}

	if (!config.entry_scripts.empty()) {
		std::string error;
		if (!sp->LoadScripts(config.entry_scripts, &error)) {
			space::SpaceId id = sp->GetId();
			manager.DestroySpace(id);
			lua_pushnil(L);
			lua_pushlstring(L, error.data(), error.size());
			return 2;
		}
	}

	lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
	return 1;
}

// space.get(id) -> space info table or nil
int l_space_get(lua_State* L) {
	space::SpaceId id = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
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
	space::SpaceId id = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
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
	space::SpaceId target = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
	entity::EntityId target_entity =
		static_cast<entity::EntityId>(luaL_checkinteger(L, 2));
	size_t len = 0;
	const char* payload = luaL_checklstring(L, 3, &len);

	if (len > kMaxSpacePayloadBytes) {
		std::string message = "space payload exceeds maximum size (" +
							  std::to_string(len) + " > " +
							  std::to_string(kMaxSpacePayloadBytes) + ")";
		return luaL_error(L, "%s", message.c_str());
	}

	space::SpaceMessage msg;
	msg.target_space = target;
	msg.target_entity = target_entity;
	msg.payload.assign(payload, len);

	if (auto* current = GetCurrentSpace(L)) {
		msg.source_space = current->GetId();
	}
	if (!lua_isnoneornil(L, 4)) {
		msg.source_entity = static_cast<entity::EntityId>(luaL_checkinteger(L, 4));
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

}  // namespace script
}  // namespace engine
