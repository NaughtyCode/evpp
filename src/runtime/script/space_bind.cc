#include "runtime/script/space_bind.h"

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

// space.create(name, config)
// config: { max_entities = N, max_players = N, scripts = {...} }
int l_space_create(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	space::SpaceConfig config;
	config.name = name;

	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "max_entities");
		if (lua_isinteger(L, -1)) {
			config.max_entities = static_cast<size_t>(lua_tointeger(L, -1));
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "max_players");
		if (lua_isinteger(L, -1)) {
			config.max_players = static_cast<size_t>(lua_tointeger(L, -1));
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "scripts");
		if (lua_istable(L, -1)) {
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				if (lua_isstring(L, -1)) {
					config.entry_scripts.push_back(lua_tostring(L, -1));
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}

	auto* sp = space::SpaceManager::Instance().CreateSpace(config);
	if (!sp) {
		lua_pushnil(L);
		lua_pushstring(L, "failed to create space");
		return 2;
	}

	// Load entry scripts if provided
	if (!config.entry_scripts.empty()) {
		sp->LoadScripts(config.entry_scripts);
	}

	lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
	return 1;
}

// space.get(id) → space info table or nil
int l_space_get(lua_State* L) {
	space::SpaceId id = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
	auto* sp = space::SpaceManager::Instance().GetSpace(id);
	if (!sp) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
	lua_setfield(L, -2, "id");
	lua_pushstring(L, sp->GetName().c_str());
	lua_setfield(L, -2, "name");
	lua_pushinteger(L, static_cast<lua_Integer>(sp->EntityCount()));
	lua_setfield(L, -2, "entity_count");
	return 1;
}

// space.destroy(id)
int l_space_destroy(lua_State* L) {
	space::SpaceId id = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
	space::SpaceManager::Instance().DestroySpace(id);
	return 0;
}

// space.send(space_id, target_entity, payload)
int l_space_send(lua_State* L) {
	space::SpaceId target = static_cast<space::SpaceId>(luaL_checkinteger(L, 1));
	entity::EntityId target_entity =
		static_cast<entity::EntityId>(luaL_checkinteger(L, 2));
	size_t len;
	const char* payload = luaL_checklstring(L, 3, &len);

	space::SpaceMessage msg;
	msg.target_space = target;
	msg.target_entity = target_entity;
	msg.payload.assign(payload, len);

	space::SpaceMessageRouter::Instance().SendMessage(std::move(msg));
	return 0;
}

// space.list() → array of space info tables
int l_space_list(lua_State* L) {
	lua_newtable(L);
	int idx = 1;
	space::SpaceManager::Instance().ForEachSpace([&](space::Space& sp) {
		lua_newtable(L);
		lua_pushinteger(L, static_cast<lua_Integer>(sp.GetId()));
		lua_setfield(L, -2, "id");
		lua_pushstring(L, sp.GetName().c_str());
		lua_setfield(L, -2, "name");
		lua_pushinteger(L, static_cast<lua_Integer>(sp.EntityCount()));
		lua_setfield(L, -2, "entity_count");
		lua_rawseti(L, -2, idx++);
	});
	return 1;
}

// space.current() → info for the default space
int l_space_current(lua_State* L) {
	auto* sp = space::SpaceManager::Instance().GetDefaultSpace();
	if (!sp) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);
	lua_pushinteger(L, static_cast<lua_Integer>(sp->GetId()));
	lua_setfield(L, -2, "id");
	lua_pushstring(L, sp->GetName().c_str());
	lua_setfield(L, -2, "name");
	lua_pushinteger(L, static_cast<lua_Integer>(sp->EntityCount()));
	lua_setfield(L, -2, "entity_count");
	return 1;
}

// Internal: space._deliver_message(src_space, src_entity, tgt_entity, payload)
// Called by SpaceMessageRouter to push a cross-space message into Lua.
int l_space_deliver_message(lua_State* L) {
	// stack: src_space, src_entity, tgt_entity, payload
	// Look up the target entity and call its on_message handler.
	// Best-effort: push to a global message queue that script polls.

	lua_getglobal(L, "space");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	lua_getfield(L, -1, "_pending_messages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);  // _pending_messages (non-table)
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, "_pending_messages");
	}
	// stack: ..., space_table, pending_table
	lua_pop(L, 1);  // space_table
	// stack: args, pending_table

	// pending_table[#pending_table + 1] = { src_space, src_entity, tgt_entity, payload }
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "source_space");
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "source_entity");
	lua_pushvalue(L, 3);
	lua_setfield(L, -2, "target_entity");
	lua_pushvalue(L, 4);
	lua_setfield(L, -2, "payload");

	lua_pushinteger(L, static_cast<lua_Integer>(luaL_len(L, -2) + 1));
	lua_insert(L, -2);
	lua_settable(L, -3);

	return 0;
}

// space.poll() → next pending message or nil
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

	// Pop first message
	lua_rawgeti(L, -1, 1);
	// Remove it from the table
	for (lua_Integer i = 1; i < len; ++i) {
		lua_rawgeti(L, -2, i + 1);
		lua_rawseti(L, -3, i);
	}
	lua_pushnil(L);
	lua_rawseti(L, -3, static_cast<int>(len));

	lua_insert(L, -2);  // move message above pending table
	lua_pop(L, 1);       // pending table
	lua_pop(L, 1);       // space table
	return 1;
}

// _on_connection_data(conn_lightuserdata, data_string)
// Default no-op; game scripts override this in the space table.
int l_space_on_connection_data(lua_State* L) {
	// Default: no-op. Override in Lua: space._on_connection_data = function(...)
	return 0;
}

static const luaL_Reg kSpaceFuncs[] = {
	{"create",             l_space_create},
	{"get",                l_space_get},
	{"destroy",            l_space_destroy},
	{"send",               l_space_send},
	{"list",               l_space_list},
	{"current",            l_space_current},
	{"poll",               l_space_poll},
	{"_deliver_message",   l_space_deliver_message},
	{"_on_connection_data", l_space_on_connection_data},
	{nullptr, nullptr}
};

}  // namespace

void ExportSpace(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);  // space module table

	// Register functions
	for (const luaL_Reg* r = kSpaceFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}

	// _pending_messages array for polling
	lua_newtable(L);
	lua_setfield(L, -2, "_pending_messages");

	lua_setglobal(L, "space");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "Space API exported to Lua");
}

}  // namespace script
}  // namespace engine
