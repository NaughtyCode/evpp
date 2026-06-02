#include "runtime/script/aoi_bind.h"

#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include "runtime/aoi/aoi_manager.h"
#include "runtime/aoi/spatial_index.h"
#include "runtime/core/log/log.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace engine {
namespace script {

namespace {

struct AOIBindState {
	lua_State* L = nullptr;
	std::unique_ptr<aoi::AOIManager> manager;
	int callback_ref = LUA_NOREF;
	int callback_depth = 0;
};

char kAOIStateRegistryKey;
constexpr const char* kAOIStateMeta = "engine.aoi.state";

float CheckFiniteFloat(lua_State* L, int index, const char* what) {
	const auto value = static_cast<double>(luaL_checknumber(L, index));
	const double max_float = static_cast<double>(std::numeric_limits<float>::max());
	if (!std::isfinite(value) || value < -max_float || value > max_float) {
		luaL_argerror(L, index, what);
	}
	return static_cast<float>(value);
}

float CheckPositiveFloat(lua_State* L, int index, const char* what) {
	const float value = CheckFiniteFloat(L, index, what);
	if (value <= 0.0f) {
		luaL_argerror(L, index, what);
	}
	return value;
}

float CheckNonNegativeFloat(lua_State* L, int index, const char* what) {
	const float value = CheckFiniteFloat(L, index, what);
	if (value < 0.0f) {
		luaL_argerror(L, index, what);
	}
	return value;
}

float OptPositiveFloat(lua_State* L, int index, float default_value, const char* what) {
	if (lua_isnoneornil(L, index)) return default_value;
	return CheckPositiveFloat(L, index, what);
}

float OptNonNegativeFloat(lua_State* L, int index, float default_value, const char* what) {
	if (lua_isnoneornil(L, index)) return default_value;
	return CheckNonNegativeFloat(L, index, what);
}

entity::EntityId CheckEntityId(lua_State* L, int index) {
	const lua_Integer raw = luaL_checkinteger(L, index);
	if (raw <= 0) {
		luaL_argerror(L, index, "entity_id must be a positive integer");
	}
	return static_cast<entity::EntityId>(raw);
}

AOIBindState* StateFromUpvalue(lua_State* L) {
	return static_cast<AOIBindState*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int PushNotInitialized(lua_State* L) {
	lua_pushnil(L);
	lua_pushstring(L, "AOI not initialized");
	return 2;
}

int PushCallbackMutationError(lua_State* L) {
	lua_pushnil(L);
	lua_pushstring(L, "AOI mutation is not allowed from AOI callback");
	return 2;
}

bool IsInCallback(const AOIBindState* state) {
	return state && state->callback_depth > 0;
}

void ClearCallback(lua_State* L, AOIBindState& state) {
	if (state.manager) {
		state.manager->SetEventCallback(nullptr);
	}
	if (state.callback_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, state.callback_ref);
		state.callback_ref = LUA_NOREF;
	}
}

void ShutdownState(lua_State* L, AOIBindState& state) {
	ClearCallback(L, state);
	state.manager.reset();
}

int l_aoi_state_gc(lua_State* L) {
	auto* state = static_cast<AOIBindState*>(lua_touserdata(L, 1));
	if (!state) return 0;

	ShutdownState(L, *state);
	state->~AOIBindState();
	return 0;
}

AOIBindState* GetOrCreateState(lua_State* L) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, &kAOIStateRegistryKey);
	auto* state = static_cast<AOIBindState*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	if (state) return state;

	void* storage = lua_newuserdata(L, sizeof(AOIBindState));
	state = new (storage) AOIBindState{};
	state->L = L;

	if (luaL_newmetatable(L, kAOIStateMeta)) {
		lua_pushcfunction(L, l_aoi_state_gc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &kAOIStateRegistryKey);
	return state;
}

void PushEntityList(lua_State* L, const std::vector<entity::EntityId>& entities) {
	lua_newtable(L);
	for (size_t i = 0; i < entities.size(); ++i) {
		lua_pushinteger(L, static_cast<lua_Integer>(entities[i]));
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

int PushException(lua_State* L, const char* prefix, const std::exception& ex) {
	lua_pushnil(L);
	lua_pushfstring(L, "%s: %s", prefix, ex.what());
	return 2;
}

/** aoi.init(world_width, world_height, cell_size) */
int l_aoi_init(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	const float world_width = CheckPositiveFloat(L, 1, "world_width must be finite and positive");
	const float world_height = CheckPositiveFloat(L, 2, "world_height must be finite and positive");
	const float cell_size = OptPositiveFloat(L, 3, 50.0f,
											 "cell_size must be finite and positive");

	try {
		auto grid = std::make_unique<aoi::SpatialGrid>(world_width, world_height, cell_size);
		auto manager = std::make_unique<aoi::AOIManager>(std::move(grid));
		ShutdownState(L, *state);
		state->manager = std::move(manager);
	} catch (const std::exception& ex) {
		return PushException(L, "AOI init failed", ex);
	}

	lua_pushboolean(L, 1);
	return 1;
}

/** aoi.set_event_callback(function | nil) */
int l_aoi_set_event_callback(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	if (lua_isnoneornil(L, 1)) {
		ClearCallback(L, *state);
		lua_pushboolean(L, 1);
		return 1;
	}

	luaL_checktype(L, 1, LUA_TFUNCTION);
	ClearCallback(L, *state);

	lua_pushvalue(L, 1);
	state->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	state->manager->SetEventCallback([state](entity::EntityId observer,
											 entity::EntityId target,
											 bool entered) {
		if (!state || !state->L || state->callback_ref == LUA_NOREF) return;

		lua_State* L = state->L;
		const int base_top = lua_gettop(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, state->callback_ref);
		lua_pushinteger(L, static_cast<lua_Integer>(observer));
		lua_pushinteger(L, static_cast<lua_Integer>(target));
		lua_pushboolean(L, entered ? 1 : 0);

		++state->callback_depth;
		const int msgh = PushLuaErrorHandlerForCall(L, 3);
		const int rc = lua_pcall(L, 3, 0, msgh);
		--state->callback_depth;

		if (rc != LUA_OK) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger, "AOI callback error: {}", lua_tostring(L, -1));
		}
		lua_settop(L, base_top);
	});

	lua_pushboolean(L, 1);
	return 1;
}

/** aoi.register_entity(entity_id, x, y, aoi_radius) */
int l_aoi_register_entity(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	const entity::EntityId id = CheckEntityId(L, 1);
	const float x = CheckFiniteFloat(L, 2, "x must be finite");
	const float y = CheckFiniteFloat(L, 3, "y must be finite");
	const float radius = OptNonNegativeFloat(L, 4, 100.0f,
											 "aoi_radius must be finite and non-negative");

	try {
		state->manager->UpsertEntity(id, x, y, radius);
	} catch (const std::exception& ex) {
		return PushException(L, "AOI register_entity failed", ex);
	}
	return 0;
}

/** aoi.update_entity(entity_id, x, y) */
int l_aoi_update_entity(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	const entity::EntityId id = CheckEntityId(L, 1);
	const float x = CheckFiniteFloat(L, 2, "x must be finite");
	const float y = CheckFiniteFloat(L, 3, "y must be finite");

	try {
		state->manager->OnEntityMove(id, x, y);
	} catch (const std::exception& ex) {
		return PushException(L, "AOI update_entity failed", ex);
	}
	return 0;
}

/** aoi.update_radius(entity_id, aoi_radius) */
int l_aoi_update_radius(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	const entity::EntityId id = CheckEntityId(L, 1);
	const float radius = CheckNonNegativeFloat(L, 2,
											   "aoi_radius must be finite and non-negative");

	try {
		state->manager->UpdateEntityRadius(id, radius);
	} catch (const std::exception& ex) {
		return PushException(L, "AOI update_radius failed", ex);
	}
	return 0;
}

/** aoi.unregister_entity(entity_id) */
int l_aoi_unregister_entity(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) return PushNotInitialized(L);
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	const entity::EntityId id = CheckEntityId(L, 1);
	state->manager->UnregisterEntity(id);
	return 0;
}

/** aoi.get_visible(entity_id) -> {entity_id, ...} */
int l_aoi_get_visible(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) {
		lua_newtable(L);
		return 1;
	}

	const entity::EntityId id = CheckEntityId(L, 1);
	PushEntityList(L, state->manager->GetVisibleEntities(id));
	return 1;
}

/** aoi.query_radius(x, y, radius) -> {entity_id, ...} */
int l_aoi_query_radius(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state || !state->manager) {
		lua_newtable(L);
		return 1;
	}

	const float x = CheckFiniteFloat(L, 1, "x must be finite");
	const float y = CheckFiniteFloat(L, 2, "y must be finite");
	const float radius = CheckNonNegativeFloat(L, 3, "radius must be finite and non-negative");

	PushEntityList(L, state->manager->QueryRadius(x, y, radius));
	return 1;
}

/** aoi.count() -> number of registered entities */
int l_aoi_count(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	const size_t count = (state && state->manager) ? state->manager->EntityCount() : 0;
	lua_pushinteger(L, static_cast<lua_Integer>(count));
	return 1;
}

/** aoi.shutdown() */
int l_aoi_shutdown(lua_State* L) {
	auto* state = StateFromUpvalue(L);
	if (!state) return 0;
	if (IsInCallback(state)) return PushCallbackMutationError(L);

	ShutdownState(L, *state);
	return 0;
}

static const luaL_Reg kAOIFuncs[] = {
	{"init",               l_aoi_init},
	{"set_event_callback", l_aoi_set_event_callback},
	{"register_entity",    l_aoi_register_entity},
	{"update_entity",      l_aoi_update_entity},
	{"update_radius",      l_aoi_update_radius},
	{"unregister_entity",  l_aoi_unregister_entity},
	{"get_visible",        l_aoi_get_visible},
	{"query_radius",       l_aoi_query_radius},
	{"count",              l_aoi_count},
	{"shutdown",           l_aoi_shutdown},
	{nullptr, nullptr}
};

}  /* namespace */

void ExportAOI(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	auto* state = GetOrCreateState(L);

	lua_newtable(L);
	for (const luaL_Reg* r = kAOIFuncs; r->name; ++r) {
		lua_pushlightuserdata(L, state);
		lua_pushcclosure(L, r->func, 1);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "aoi");

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "AOI API exported to Lua");
}

}  /* namespace script */
}  /* namespace engine */
