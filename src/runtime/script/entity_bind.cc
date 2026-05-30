#include "runtime/script/entity_bind.h"

#include <cinttypes>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "runtime/core/log/log.h"
#include "runtime/entity/entity.h"
#include "runtime/entity/entity_manager.h"
#include "runtime/vm/lua_error_handler.h"
#include "runtime/vm/vm.h"

extern "C" {
#include "lauxlib.h"
}

namespace engine {
namespace script {

namespace {

using entity::Entity;
using entity::EntityId;
using entity::EntityManager;
using entity::EntityState;

const char* kEntityMetaName = "entity.instance";

struct LuaRegistryRef {
	lua_State* L = nullptr;
	int ref = LUA_NOREF;

	~LuaRegistryRef() { Release(); }

	void Release() {
		if (L && ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			ref = LUA_NOREF;
		}
	}
};

struct EntityCtx {
	EntityId id = entity::kInvalidEntityId;
	lua_State* L = nullptr;
	bool disposed = false;
	int conn_ref = LUA_NOREF;  // Lua conn instance table (for send)
	std::unordered_map<TimerId, std::shared_ptr<LuaRegistryRef>> timer_refs;
};

std::unordered_map<EntityId, EntityCtx*> g_entity_contexts;

EntityCtx* GetEntityCtx(lua_State* L, int idx) {
	lua_getfield(L, idx, "_ctx");
	auto* ctx = static_cast<EntityCtx*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return ctx;
}

bool IsValidLuaRef(int ref) {
	return ref != LUA_NOREF && ref != LUA_REFNIL;
}

void ReleaseEntityLuaComponents(lua_State* L, Entity* entity) {
	if (!L || !entity) return;
	for (auto& pair : entity->TakeLuaComponents()) {
		if (IsValidLuaRef(pair.second)) {
			luaL_unref(L, LUA_REGISTRYINDEX, pair.second);
		}
	}
}

void ReleaseEntityCtxRefs(lua_State* L, EntityCtx* ctx) {
	if (!ctx) return;
	if (!L) L = ctx->L;

	for (auto& pair : ctx->timer_refs) {
		if (pair.second) {
			pair.second->Release();
		}
	}
	ctx->timer_refs.clear();

	if (L && IsValidLuaRef(ctx->conn_ref)) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->conn_ref);
		ctx->conn_ref = LUA_NOREF;
	}

	ReleaseEntityLuaComponents(L, EntityManager::Instance().GetEntity(ctx->id));
}

void EraseTimerRef(EntityId eid, TimerId tid) {
	auto it = g_entity_contexts.find(eid);
	if (it == g_entity_contexts.end() || !it->second) return;
	it->second->timer_refs.erase(tid);
}

void EraseEntityContext(EntityCtx* ctx) {
	if (!ctx) return;
	auto it = g_entity_contexts.find(ctx->id);
	if (it != g_entity_contexts.end() && it->second == ctx) {
		g_entity_contexts.erase(it);
	}
}

void OnEntityDestroyFromManager(Entity& entity) {
	auto it = g_entity_contexts.find(entity.GetId());
	if (it == g_entity_contexts.end() || !it->second || it->second->disposed) return;
	auto* ctx = it->second;
	ctx->disposed = true;
	ReleaseEntityCtxRefs(ctx->L, ctx);
}

// ── entity.create([id])  - entity_instance ─────────────────────────────

int l_entity_create(lua_State* L) {
	EntityId id = 0;
	if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
		id = static_cast<EntityId>(luaL_checkinteger(L, 1));
	}

	auto* entity = EntityManager::Instance().CreateEntity(id);
	if (!entity) {
		return luaL_error(L, "entity id %" PRIu64 " already exists",
						  static_cast<uint64_t>(id));
	}

	auto* ctx = CLOUDENGINE_MEM_NEW(EntityCtx);
	ctx->id = entity->GetId();
	ctx->L = L;
	g_entity_contexts[ctx->id] = ctx;

	// Build Lua instance table
	lua_newtable(L);
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, -2, "_ctx");

	luaL_getmetatable(L, kEntityMetaName);
	lua_setmetatable(L, -2);

	// entity: Activate by default for convenience
	entity->Activate();

	return 1;
}

// ── entity:destroy()  - bool ──────────────────────────────────────────

int l_entity_destroy(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) {
		lua_pushboolean(L, 0);
		return 1;
	}

	ctx->disposed = true;
	ReleaseEntityCtxRefs(L, ctx);

	EntityManager::Instance().DestroyEntity(ctx->id);
	EraseEntityContext(ctx);

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	CLOUDENGINE_MEM_DELETE(ctx);

	lua_pushboolean(L, 1);
	return 1;
}

// ── entity:get_id()  - integer ────────────────────────────────────────

int l_entity_get_id(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	lua_pushinteger(L, static_cast<lua_Integer>(ctx->id));
	return 1;
}

// ── entity:get_state()  - string ─────────────────────────────────────

int l_entity_get_state(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	switch (entity->GetState()) {
	case EntityState::Created:   lua_pushstring(L, "created"); break;
	case EntityState::Active:    lua_pushstring(L, "active"); break;
	case EntityState::Suspended: lua_pushstring(L, "suspended"); break;
	case EntityState::Destroyed: lua_pushstring(L, "destroyed"); break;
	default:                     lua_pushstring(L, "unknown"); break;
	}
	return 1;
}

// ── entity:activate() / entity:suspend() ─────────────────────────────

int l_entity_activate(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");
	entity->Activate();
	return 0;
}

int l_entity_suspend(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");
	entity->Suspend();
	return 0;
}

// ── entity:get_attr(key)  - value ─────────────────────────────────────

int l_entity_get_attr(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* key = luaL_checkstring(L, 2);
	const auto* val = entity->Attrs().TryGet(key);
	if (!val) {
		lua_pushnil(L);
		return 1;
	}

	// Convert AttrValue variant to Lua value
	std::visit([L](auto&& v) {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<T, int64_t>) {
			lua_pushinteger(L, static_cast<lua_Integer>(v));
		} else if constexpr (std::is_same_v<T, double>) {
			lua_pushnumber(L, v);
		} else if constexpr (std::is_same_v<T, std::string>) {
			lua_pushstring(L, v.c_str());
		} else if constexpr (std::is_same_v<T, bool>) {
			lua_pushboolean(L, v ? 1 : 0);
		}
	}, *val);

	return 1;
}

// ── entity:set_attr(key, value) ──────────────────────────────────────

int l_entity_set_attr(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* key = luaL_checkstring(L, 2);
	int val_type = lua_type(L, 3);

	if (val_type == LUA_TNIL) {
		entity->Attrs().Remove(key);
		return 0;
	}

	entity::AttrValue val;
	switch (val_type) {
	case LUA_TNUMBER:
		if (lua_isinteger(L, 3)) {
			val = static_cast<int64_t>(lua_tointeger(L, 3));
		} else {
			val = static_cast<double>(lua_tonumber(L, 3));
		}
		break;
	case LUA_TSTRING: {
		size_t len;
		const char* s = lua_tolstring(L, 3, &len);
		val = std::string(s, len);
		break;
	}
	case LUA_TBOOLEAN:
		val = static_cast<bool>(lua_toboolean(L, 3));
		break;
	default:
		return luaL_error(L, "unsupported attribute type: %s", lua_typename(L, val_type));
	}

	entity->Attrs().Set(key, std::move(val));
	return 0;
}

// ── entity:has_attr(key)  - bool ─────────────────────────────────────

int l_entity_has_attr(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* key = luaL_checkstring(L, 2);
	lua_pushboolean(L, entity->Attrs().Has(key) ? 1 : 0);
	return 1;
}

// ── entity:bind_connection(conn) ──────────────────────────────────────
// Stores the Lua conn table so entity:send() delegates to conn:send().

int l_entity_bind_connection(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");

	if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
		// Unbind
		if (IsValidLuaRef(ctx->conn_ref)) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->conn_ref);
			ctx->conn_ref = LUA_NOREF;
		}
		return 0;
	}

	luaL_checktype(L, 2, LUA_TTABLE);

	// Release old conn ref
	if (IsValidLuaRef(ctx->conn_ref)) {
		luaL_unref(L, LUA_REGISTRYINDEX, ctx->conn_ref);
		ctx->conn_ref = LUA_NOREF;
	}

	// Store new conn table
	lua_pushvalue(L, 2);
	ctx->conn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	return 0;
}

// ── entity:get_connection()  - conn or nil ────────────────────────────

int l_entity_get_connection(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");

	if (IsValidLuaRef(ctx->conn_ref)) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->conn_ref);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// ── entity:send(data) ─────────────────────────────────────────────────
// Delegates to bound conn:send(data)

int l_entity_send(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");

	if (!IsValidLuaRef(ctx->conn_ref)) {
		return luaL_error(L, "entity has no bound connection");
	}

	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);

	// conn:send(data)
	const int base_top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->conn_ref);
	lua_getfield(L, -1, "send");
	lua_insert(L, -2);
	lua_pushlstring(L, data, len);
	int msgh = PushLuaErrorHandlerForCall(L, 2);
	if (lua_pcall(L, 2, 0, msgh) != LUA_OK) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "[entity] send error: {}", lua_tostring(L, -1));
	}
	lua_settop(L, base_top);

	return 0;
}

// ── entity:add_timer(interval_ms, repeat, callback)  - timer_id ───────

int l_entity_add_timer(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");

	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	int64_t interval_ms = luaL_checkinteger(L, 2);
	bool repeat = lua_toboolean(L, 3) != 0;
	luaL_checktype(L, 4, LUA_TFUNCTION);

	lua_pushvalue(L, 4);
	auto cb_ref = std::make_shared<LuaRegistryRef>();
	cb_ref->L = L;
	cb_ref->ref = luaL_ref(L, LUA_REGISTRYINDEX);

	EntityId eid = ctx->id;
	auto tid_holder = std::make_shared<TimerId>(kInvalidTimerId);

	auto timer_cb = [L, eid, cb_ref, repeat, tid_holder]() {
		auto* ent = EntityManager::Instance().GetEntity(eid);
		if (!ent || ent->GetState() != EntityState::Active) {
			if (!repeat) {
				cb_ref->Release();
				EraseTimerRef(eid, *tid_holder);
			}
			return;
		}
		if (cb_ref->ref == LUA_NOREF) return;

		const int base_top = lua_gettop(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref->ref);
		int msgh = PushLuaErrorHandlerForCall(L, 0);
		if (lua_pcall(L, 0, 0, msgh) != LUA_OK) {
			auto* logger = GetLogger();
			ENGINE_LOG_ERROR(logger, "[entity] timer callback error: {}",
							 lua_tostring(L, -1));
		}
		lua_settop(L, base_top);
		if (!repeat) {
			cb_ref->Release();
			EraseTimerRef(eid, *tid_holder);
		}
	};

	TimerId tid = entity->AddTimer(interval_ms, repeat, std::move(timer_cb));
	if (tid == kInvalidTimerId) {
		cb_ref->Release();
		return luaL_error(L, "entity timer manager is not initialized or interval is invalid");
	}

	*tid_holder = tid;
	ctx->timer_refs[tid] = cb_ref;

	lua_pushinteger(L, static_cast<lua_Integer>(tid));
	return 1;
}

// ── entity:cancel_timer(timer_id) ─────────────────────────────────────

int l_entity_cancel_timer(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");

	TimerId tid = static_cast<TimerId>(luaL_checkinteger(L, 2));

	// Release Lua callback ref if tracked
	auto it = ctx->timer_refs.find(tid);
	if (it != ctx->timer_refs.end()) {
		if (it->second) {
			it->second->Release();
		}
		ctx->timer_refs.erase(it);
	}

	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (entity) {
		entity->CancelTimer(tid);
	}

	return 0;
}

// ── entity:add_component(name, component_table) ──────────────────────

int l_entity_add_component(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);

	// Remove old component if exists
	int old_ref = entity->GetLuaComponent(name);
	if (IsValidLuaRef(old_ref)) {
		luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
	}

	lua_pushvalue(L, 3);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	entity->AddLuaComponent(name, ref);

	return 0;
}

// ── entity:get_component(name)  - table or nil ────────────────────────

int l_entity_get_component(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* name = luaL_checkstring(L, 2);
	int ref = entity->GetLuaComponent(name);
	if (IsValidLuaRef(ref)) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// ── entity:remove_component(name) ────────────────────────────────────

int l_entity_remove_component(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx || ctx->disposed) return luaL_error(L, "entity: invalid context");
	auto* entity = EntityManager::Instance().GetEntity(ctx->id);
	if (!entity) return luaL_error(L, "entity not found");

	const char* name = luaL_checkstring(L, 2);
	int ref = entity->GetLuaComponent(name);
	if (IsValidLuaRef(ref)) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		entity->RemoveLuaComponent(name);
	}

	return 0;
}

// ── __gc metamethod ──────────────────────────────────────────────────

int l_entity_gc(lua_State* L) {
	auto* ctx = GetEntityCtx(L, 1);
	if (!ctx) return 0;

	if (!ctx->disposed) {
		ctx->disposed = true;
		ReleaseEntityCtxRefs(L, ctx);
		EntityManager::Instance().DestroyEntity(ctx->id);
	}

	EraseEntityContext(ctx);

	lua_pushnil(L);
	lua_setfield(L, 1, "_ctx");

	CLOUDENGINE_MEM_DELETE(ctx);
	return 0;
}

// ── Method tables ─────────────────────────────────────────────────────

const luaL_Reg kEntityMethods[] = {
	{"destroy", l_entity_destroy},
	{"get_id", l_entity_get_id},
	{"get_state", l_entity_get_state},
	{"activate", l_entity_activate},
	{"suspend", l_entity_suspend},
	{"get_attr", l_entity_get_attr},
	{"set_attr", l_entity_set_attr},
	{"has_attr", l_entity_has_attr},
	{"bind_connection", l_entity_bind_connection},
	{"get_connection", l_entity_get_connection},
	{"send", l_entity_send},
	{"add_timer", l_entity_add_timer},
	{"cancel_timer", l_entity_cancel_timer},
	{"add_component", l_entity_add_component},
	{"get_component", l_entity_get_component},
	{"remove_component", l_entity_remove_component},
	{nullptr, nullptr},
};

const luaL_Reg kEntityFunctions[] = {
	{"create", l_entity_create},
	{nullptr, nullptr},
};

}  // namespace

void ExportEntity(ScriptVM& vm) {
	auto* L = vm.GetState();
	if (!L) return;

	// Register entity instance metatable
	luaL_newmetatable(L, kEntityMetaName);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, kEntityMethods, 0);
	lua_pushcfunction(L, l_entity_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	// Register global "entity" module with static functions
	luaL_newlib(L, kEntityFunctions);
	lua_setglobal(L, "entity");

	EntityManager::Instance().SetDestroyHook(OnEntityDestroyFromManager);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "ScriptBind: entity module exported");
}

void ShutdownEntityBindings() {
	auto* logger = GetLogger();
	for (auto& pair : g_entity_contexts) {
		auto* ctx = pair.second;
		if (!ctx || ctx->disposed) continue;
		ctx->disposed = true;
		ReleaseEntityCtxRefs(ctx->L, ctx);
	}

	size_t count = EntityManager::Instance().Count();
	if (count > 0) {
		EntityManager::Instance().DestroyAll();
		ENGINE_LOG_INFO(logger, "ScriptBind: destroyed [{}] entities", count);
	} else {
		ENGINE_LOG_DEBUG(logger, "ScriptBind: no active entities to shut down");
	}
}

}  // namespace script
}  // namespace engine
