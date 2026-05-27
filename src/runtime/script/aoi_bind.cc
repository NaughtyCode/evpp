#include "runtime/script/aoi_bind.h"

#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/core/log/log.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

std::unique_ptr<aoi::AOIManager> g_aoi_manager;

// aoi.init(world_width, world_height, cell_size)
int l_aoi_init(lua_State* L) {
	float world_width = static_cast<float>(luaL_checknumber(L, 1));
	float world_height = static_cast<float>(luaL_checknumber(L, 2));
	float cell_size = static_cast<float>(luaL_optnumber(L, 3, 50.0));

	auto grid = std::make_unique<aoi::SpatialGrid>(world_width, world_height, cell_size);
	g_aoi_manager = std::make_unique<aoi::AOIManager>(std::move(grid));

	g_aoi_manager->SetEventCallback([](entity::EntityId observer,
										entity::EntityId target, bool entered) {
		auto* logger = GetLogger();
		ENGINE_LOG_DEBUG(logger, "AOI: entity [{}] {} [{}]",
						 observer, entered ? "sees" : "loses", target);
	});

	lua_pushboolean(L, 1);
	return 1;
}

// aoi.register_entity(entity_id, x, y, aoi_radius)
int l_aoi_register_entity(lua_State* L) {
	if (!g_aoi_manager) {
		lua_pushnil(L);
		lua_pushstring(L, "AOI not initialized");
		return 2;
	}

	entity::EntityId id = static_cast<entity::EntityId>(luaL_checkinteger(L, 1));
	float x = static_cast<float>(luaL_checknumber(L, 2));
	float y = static_cast<float>(luaL_checknumber(L, 3));
	float radius = static_cast<float>(luaL_optnumber(L, 4, 100.0f));

	g_aoi_manager->RegisterEntity(id, radius);
	g_aoi_manager->OnEntityMove(id, x, y);
	return 0;
}

// aoi.update_entity(entity_id, x, y)
int l_aoi_update_entity(lua_State* L) {
	if (!g_aoi_manager) return 0;

	entity::EntityId id = static_cast<entity::EntityId>(luaL_checkinteger(L, 1));
	float x = static_cast<float>(luaL_checknumber(L, 2));
	float y = static_cast<float>(luaL_checknumber(L, 3));

	g_aoi_manager->OnEntityMove(id, x, y);
	return 0;
}

// aoi.unregister_entity(entity_id)
int l_aoi_unregister_entity(lua_State* L) {
	if (!g_aoi_manager) return 0;

	entity::EntityId id = static_cast<entity::EntityId>(luaL_checkinteger(L, 1));
	g_aoi_manager->UnregisterEntity(id);
	return 0;
}

// aoi.get_visible(entity_id) → {entity_id, ...}
int l_aoi_get_visible(lua_State* L) {
	if (!g_aoi_manager) {
		lua_newtable(L);
		return 1;
	}

	entity::EntityId id = static_cast<entity::EntityId>(luaL_checkinteger(L, 1));
	auto visible = g_aoi_manager->GetVisibleEntities(id);

	lua_newtable(L);
	for (size_t i = 0; i < visible.size(); ++i) {
		lua_pushinteger(L, static_cast<lua_Integer>(visible[i]));
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}

// aoi.query_radius(x, y, radius) → {entity_id, ...}
int l_aoi_query_radius(lua_State* L) {
	if (!g_aoi_manager) {
		lua_newtable(L);
		return 1;
	}

	float x = static_cast<float>(luaL_checknumber(L, 1));
	float y = static_cast<float>(luaL_checknumber(L, 2));
	float radius = static_cast<float>(luaL_checknumber(L, 3));

	auto results = g_aoi_manager->QueryRadius(x, y, radius);

	lua_newtable(L);
	for (size_t i = 0; i < results.size(); ++i) {
		lua_pushinteger(L, static_cast<lua_Integer>(results[i]));
		lua_rawseti(L, -2, static_cast<int>(i + 1));
	}
	return 1;
}

// aoi.count() → number of registered entities
int l_aoi_count(lua_State* L) {
	if (!g_aoi_manager) {
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, static_cast<lua_Integer>(g_aoi_manager->EntityCount()));
	return 1;
}

// aoi.shutdown()
int l_aoi_shutdown(lua_State* L) {
	g_aoi_manager.reset();
	return 0;
}

static const luaL_Reg kAOIFuncs[] = {
	{"init",              l_aoi_init},
	{"register_entity",   l_aoi_register_entity},
	{"update_entity",     l_aoi_update_entity},
	{"unregister_entity", l_aoi_unregister_entity},
	{"get_visible",       l_aoi_get_visible},
	{"query_radius",      l_aoi_query_radius},
	{"count",             l_aoi_count},
	{"shutdown",          l_aoi_shutdown},
	{nullptr, nullptr}
};

}  // namespace

void ExportAOI(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	lua_newtable(L);
	for (const luaL_Reg* r = kAOIFuncs; r->name; ++r) {
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "aoi");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "AOI API exported to Lua");
}

}  // namespace script
}  // namespace engine
