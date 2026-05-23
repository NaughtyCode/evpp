#ifdef ENGINE_PHYSICS_ENABLED

#include "engine/physics/physics_bindings.h"

#include <cstring>

#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

#include "engine/physics/physics_system.h"
#include "engine/vm/vm.h"

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
// physics.get_transform(body_id) → (x, y, z, qx, qy, qz, qw) | nil
//============================================================================

int LuaGetTransform(lua_State* L) {
    if (!CheckInit(L)) return 2;

    // This is a synchronous query against the physics world.
    // Since the Lua callback runs on the main thread after FetchResult,
    // we read from the cached transforms in the last frame result.
    // But we don't have direct access to PhysicsWorld from here.
    // For now, return nil with explanation.

    PushNilError(L, "get_transform: synchronous query not yet available");
    return 2;
}

//============================================================================
// physics.get_velocity(body_id) → (vx, vy, vz) | nil
//============================================================================

int LuaGetVelocity(lua_State* L) {
    if (!CheckInit(L)) return 2;

    PushNilError(L, "get_velocity: synchronous query not yet available");
    return 2;
}

//============================================================================
// physics.is_active(body_id) → bool
//============================================================================

int LuaIsActive(lua_State* L) {
    if (!CheckInit(L)) return 2;

    // Placeholder — synchronous query bridge to be added
    lua_pushboolean(L, 0);
    return 1;
}

//============================================================================
// physics.ray_cast(ox, oy, oz, dx, dy, dz, max_dist) → {body_id, x, y, z} | nil
//============================================================================

int LuaRayCast(lua_State* L) {
    if (!CheckInit(L)) return 2;

    PushNilError(L, "ray_cast: not yet implemented");
    return 2;
}

//============================================================================
// physics.get_stats() → {bodies=N, active=N, collisions=N}
//============================================================================

int LuaGetStats(lua_State* L) {
    if (!CheckInit(L)) return 2;

    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "bodies");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "active");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "collisions");
    return 1;
}

//============================================================================
// Module registration table
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
    {"get_stats",      LuaGetStats},
    {nullptr, nullptr}
};

} // namespace

//============================================================================
// Register — export "physics" module to ScriptVM
//============================================================================

void Register(ScriptVM& vm) {
    vm.RegisterModule("physics", kPhysicsModule);
}

} // namespace physics_bindings
} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
