#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_bindings.h"

#include <cstring>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

#include "runtime/physics/physics_log.h"
#include "runtime/physics/physics_system.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace physics_bindings {

//============================================================================
// Lua stack helpers
//============================================================================

namespace {

// Push nil + error message, return 2
int PushNilError(lua_State* L, const char* msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}

// Check if physics system is initialized; push error if not
bool CheckInit(lua_State* L) {
    if (!PhysicsSystem::Instance().IsInitialized()) {
        PushNilError(L, "physics not initialized");
        return false;
    }
    return true;
}

//============================================================================
// physics.spawn(proto_id, x, y, z, qx, qy, qz, qw[, user_data]) → body_id | nil, err
//============================================================================

int LuaSpawn(lua_State* L) {
    if (!CheckInit(L)) return 2;

    const char* proto_id = luaL_checkstring(L, 1);
    double x  = luaL_checknumber(L, 2);
    double y  = luaL_checknumber(L, 3);
    double z  = luaL_checknumber(L, 4);
    float qx  = static_cast<float>(luaL_checknumber(L, 5));
    float qy  = static_cast<float>(luaL_checknumber(L, 6));
    float qz  = static_cast<float>(luaL_checknumber(L, 7));
    float qw  = static_cast<float>(luaL_checknumber(L, 8));
    uint64_t user_data = lua_gettop(L) >= 9 ?
        static_cast<uint64_t>(luaL_checkinteger(L, 9)) : 0;

    // Note: Spawn is enqueued asynchronously. body_id is assigned by the
    // physics thread. The caller uses the returned body_id for later commands.
    // Currently, the physics thread assigns body_ids but doesn't return them
    // synchronously. We generate a placeholder and the actual body_id comes
    // back via transforms in the next frame result.
    //
    // For synchronous Lua API, we'd need a different mechanism.
    // For now, we report that spawn commands are enqueued.

    PhysicsSystem::Instance().EnqueueSpawn(proto_id, x, y, z, qx, qy, qz, qw, user_data);

    // Return a temporary body_id (0) — the real ID will appear in transforms
    // after the next Tick/FetchResult cycle.
    lua_pushinteger(L, 0);
    return 1;
}

//============================================================================
// physics.destroy(body_id) → bool
//============================================================================

int LuaDestroy(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    PhysicsSystem::Instance().EnqueueDestroy(body_id);

    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// physics.apply_force(body_id, fx, fy, fz, px, py, pz) → bool
//============================================================================

int LuaApplyForce(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float fx = static_cast<float>(luaL_checknumber(L, 2));
    float fy = static_cast<float>(luaL_checknumber(L, 3));
    float fz = static_cast<float>(luaL_checknumber(L, 4));
    double px = luaL_checknumber(L, 5);
    double py = luaL_checknumber(L, 6);
    double pz = luaL_checknumber(L, 7);

    PhysicsSystem::Instance().EnqueueApplyForce(body_id, fx, fy, fz, px, py, pz);
    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// physics.set_velocity(body_id, vx, vy, vz) → bool
//============================================================================

int LuaSetVelocity(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float vx = static_cast<float>(luaL_checknumber(L, 2));
    float vy = static_cast<float>(luaL_checknumber(L, 3));
    float vz = static_cast<float>(luaL_checknumber(L, 4));

    PhysicsSystem::Instance().EnqueueSetVelocity(body_id, vx, vy, vz);
    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// physics.get_transform(body_id) → (x, y, z, qx, qy, qz, qw) | nil, err
//============================================================================

int LuaGetTransform(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto t = PhysicsSystem::Instance().GetTransform(body_id);
    if (!t.has_value()) {
        PushNilError(L, "body not found");
        return 2;
    }
    lua_pushnumber(L, t->pos_x);
    lua_pushnumber(L, t->pos_y);
    lua_pushnumber(L, t->pos_z);
    lua_pushnumber(L, t->rot_x);
    lua_pushnumber(L, t->rot_y);
    lua_pushnumber(L, t->rot_z);
    lua_pushnumber(L, t->rot_w);
    return 7;
}

//============================================================================
// physics.get_velocity(body_id) → (vx, vy, vz) | nil, err
//============================================================================

int LuaGetVelocity(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto v = PhysicsSystem::Instance().GetVelocity(body_id);
    if (!v.has_value()) {
        PushNilError(L, "body not found");
        return 2;
    }
    lua_pushnumber(L, v->x);
    lua_pushnumber(L, v->y);
    lua_pushnumber(L, v->z);
    return 3;
}

//============================================================================
// physics.is_active(body_id) → bool | nil, err
//============================================================================

int LuaIsActive(lua_State* L) {
    if (!CheckInit(L)) return 2;

    uint32_t body_id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, PhysicsSystem::Instance().IsBodyActive(body_id) ? 1 : 0);
    return 1;
}

//============================================================================
// physics.ray_cast(ox, oy, oz, dx, dy, dz, max_dist) → {body_id, x, y, z} | nil, err
//============================================================================

int LuaRayCast(lua_State* L) {
    if (!CheckInit(L)) return 2;

    double ox = luaL_checknumber(L, 1);
    double oy = luaL_checknumber(L, 2);
    double oz = luaL_checknumber(L, 3);
    double dx = luaL_checknumber(L, 4);
    double dy = luaL_checknumber(L, 5);
    double dz = luaL_checknumber(L, 6);
    float max_dist = static_cast<float>(luaL_checknumber(L, 7));

    auto hit = PhysicsSystem::Instance().RayCast(ox, oy, oz, dx, dy, dz, max_dist);
    if (!hit.has_value()) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushinteger(L, hit->body_id);
    lua_setfield(L, -2, "body_id");
    lua_pushnumber(L, hit->x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit->y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit->z);
    lua_setfield(L, -2, "z");
    return 1;
}

//============================================================================
// physics.save_state() → string (binary blob)
//============================================================================

int LuaSaveState(lua_State* L) {
    if (!CheckInit(L)) return 2;

    std::string data = PhysicsSystem::Instance().SaveState();
    lua_pushlstring(L, data.data(), data.size());
    return 1;
}

//============================================================================
// physics.restore_state(data) → bool | nil, err
//============================================================================

int LuaRestoreState(lua_State* L) {
    if (!CheckInit(L)) return 2;

    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    bool ok = PhysicsSystem::Instance().RestoreState(std::string(data, len));
    if (!ok) {
        PushNilError(L, "restore_state failed");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// physics.recover([saved_state]) → bool | nil, err
//============================================================================

int LuaRecover(lua_State* L) {
    if (!CheckInit(L)) return 2;

    std::string saved_state;
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        size_t len = 0;
        const char* data = lua_tolstring(L, 1, &len);
        saved_state.assign(data, len);
    }

    bool ok = PhysicsSystem::Instance().Recover(saved_state);
    if (!ok) {
        PushNilError(L, "recovery failed");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

//============================================================================
// physics.get_stats() → {bodies=N, active=N, collisions=N}
//============================================================================

int LuaGetStats(lua_State* L) {
    if (!CheckInit(L)) return 2;

    auto stats = PhysicsSystem::Instance().GetPhysicsStats();
    lua_newtable(L);
    lua_pushinteger(L, stats.total_bodies);
    lua_setfield(L, -2, "bodies");
    lua_pushinteger(L, stats.active_bodies);
    lua_setfield(L, -2, "active");
    lua_pushinteger(L, stats.contact_constraints);
    lua_setfield(L, -2, "collisions");
    return 1;
}

//============================================================================
// Physics-specific log API — logger obtained via custom-ptr store from
// PhysicsThread each call, so it stays valid after PhysicsSystem::Start().
//============================================================================

#define PHYSICS_LUA_LOG_CALL(name, macro)                            \
    int LuaLog##name(lua_State* L) {                                  \
        const char* msg = luaL_checkstring(L, 1);                     \
        auto* __thread = PhysicsSystem::GetThreadFromState(L);        \
        if (__thread) {                                               \
            auto* __logger = __thread->GetLogger();                   \
            if (__logger) {                                           \
                macro(__logger, "[physics_lua] {}", msg);             \
            }                                                         \
        }                                                             \
        return 0;                                                     \
    }

PHYSICS_LUA_LOG_CALL(Trace, PHYSICS_LOG_TRACE)
PHYSICS_LUA_LOG_CALL(Debug, PHYSICS_LOG_DEBUG)
PHYSICS_LUA_LOG_CALL(Info,  PHYSICS_LOG_INFO)
PHYSICS_LUA_LOG_CALL(Warn,  PHYSICS_LOG_WARN)
PHYSICS_LUA_LOG_CALL(Error, PHYSICS_LOG_ERROR)
PHYSICS_LUA_LOG_CALL(Fatal, PHYSICS_LOG_CRITICAL)

#undef PHYSICS_LUA_LOG_CALL

//============================================================================
// Log functions — registered as globals (log_info, log_debug, …)
//============================================================================

const luaL_Reg kPhysicsLogFunctions[] = {
    {"log_trace",      LuaLogTrace},
    {"log_debug",      LuaLogDebug},
    {"log_info",       LuaLogInfo},
    {"log_warn",       LuaLogWarn},
    {"log_error",      LuaLogError},
    {"log_fatal",      LuaLogFatal},
    {nullptr, nullptr}
};

//============================================================================
// Physics module — registered as the "physics" table
//============================================================================

const luaL_Reg kPhysicsModule[] = {
    {"spawn",          LuaSpawn},
    {"destroy",        LuaDestroy},
    {"apply_force",    LuaApplyForce},
    {"set_velocity",   LuaSetVelocity},
    {"get_transform",  LuaGetTransform},
    {"get_velocity",   LuaGetVelocity},
    {"is_active",      LuaIsActive},
    {"ray_cast",       LuaRayCast},
    {"save_state",     LuaSaveState},
    {"restore_state",  LuaRestoreState},
    {"recover",        LuaRecover},
    {"get_stats",      LuaGetStats},
    {nullptr, nullptr}
};

} // namespace

//============================================================================
// Register — export "physics" module to ScriptVM
//============================================================================

void Register(ScriptVM& vm) {
    vm.RegisterFunctions(kPhysicsLogFunctions);  // globals: log_info, log_debug, …
    vm.RegisterModule("physics", kPhysicsModule);
}

} // namespace physics_bindings
} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
