#pragma once

#ifndef PHYSICS_INTERNAL_ACCESS
#error \
	"physics_bind_common.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>

#include "runtime/physics/physics_system.h"
#include "runtime/vm/vm.h"

namespace engine {
namespace physics_bindings {

struct BindingContext {
	PhysicsSystem* system = nullptr;
	PhysicsThread* thread = nullptr;
	PhysicsWorld* world = nullptr;
	PhysicsScriptVM* script_vm = nullptr;

	bool IsPhysicsThread() const {
		return thread != nullptr && thread->IsPhysicsThread();
	}
};

inline BindingContext GetContext(lua_State* L) {
	return {PhysicsSystem::GetSystemFromState(L),
			PhysicsSystem::GetThreadFromState(L),
			PhysicsSystem::GetWorldFromState(L),
			PhysicsSystem::GetScriptVMFromState(L)};
}

inline int PushNilError(lua_State* L, const char* msg) {
	lua_pushnil(L);
	lua_pushstring(L, msg);
	return 2;
}

inline bool CheckSystem(lua_State* L, BindingContext& ctx) {
	ctx = GetContext(L);
	if (!ctx.system) {
		PushNilError(L, "physics system not available");
		return false;
	}
	return true;
}

inline bool CheckInit(lua_State* L, BindingContext& ctx) {
	if (!CheckSystem(L, ctx)) return false;
	if (!ctx.system->IsInitialized()) {
		PushNilError(L, "physics not initialized");
		return false;
	}
	return true;
}

inline bool CheckWorld(lua_State* L, BindingContext& ctx) {
	if (!ctx.world) {
		PushNilError(L, "physics world not available");
		return false;
	}
	return true;
}

inline JPH::Quat NormalizedOrIdentity(float x, float y, float z, float w) {
	JPH::Quat q(x, y, z, w);
	return q.LengthSq() > 1.0e-12f ? q.Normalized() : JPH::Quat::sIdentity();
}

inline double CheckFiniteDouble(lua_State* L, int index, const char* name) {
	double value = luaL_checknumber(L, index);
	luaL_argcheck(L, std::isfinite(value), index, name);
	return value;
}

inline float CheckFiniteFloat(lua_State* L, int index, const char* name) {
	double value = CheckFiniteDouble(L, index, name);
	luaL_argcheck(L,
				  value >= -static_cast<double>((std::numeric_limits<float>::max)()) &&
					  value <= static_cast<double>((std::numeric_limits<float>::max)()),
				  index,
				  name);
	return static_cast<float>(value);
}

inline JPH::Real CheckFiniteReal(lua_State* L, int index, const char* name) {
	double value = CheckFiniteDouble(L, index, name);
	luaL_argcheck(L,
				  std::abs(value) <=
					  static_cast<double>((std::numeric_limits<JPH::Real>::max)()),
				  index,
				  name);
	return static_cast<JPH::Real>(value);
}

inline uint32_t CheckUInt32(lua_State* L, int index, const char* name) {
	lua_Integer value = luaL_checkinteger(L, index);
	luaL_argcheck(L,
				  value >= 0 &&
					  static_cast<uint64_t>(value) <=
						  static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()),
				  index,
				  name);
	return static_cast<uint32_t>(value);
}

inline uint64_t CheckUInt64(lua_State* L, int index, const char* name) {
	lua_Integer value = luaL_checkinteger(L, index);
	luaL_argcheck(L, value >= 0, index, name);
	return static_cast<uint64_t>(value);
}

inline void SetField(lua_State* L, const char* name, bool value) {
	lua_pushboolean(L, value ? 1 : 0);
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, int value) {
	lua_pushinteger(L, static_cast<lua_Integer>(value));
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, uint32_t value) {
	lua_pushinteger(L, static_cast<lua_Integer>(value));
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, uint64_t value) {
	if (value <= static_cast<uint64_t>((std::numeric_limits<lua_Integer>::max)())) {
		lua_pushinteger(L, static_cast<lua_Integer>(value));
	} else {
		lua_pushnumber(L, static_cast<lua_Number>(value));
	}
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, float value) {
	lua_pushnumber(L, static_cast<lua_Number>(value));
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, double value) {
	lua_pushnumber(L, static_cast<lua_Number>(value));
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, const char* value) {
	lua_pushstring(L, value);
	lua_setfield(L, -2, name);
}

inline void SetField(lua_State* L, const char* name, const std::string& value) {
	lua_pushlstring(L, value.data(), value.size());
	lua_setfield(L, -2, name);
}

void RegisterBodyBindings(lua_State* L);
void RegisterStateBindings(lua_State* L);
void RegisterConfigBindings(lua_State* L);
void RegisterAssetBindings(lua_State* L);
void RegisterCommandBindings(lua_State* L);
void RegisterLogGlobals(ScriptVM& vm);
void RegisterLogModuleBindings(lua_State* L);
void RegisterConstants(lua_State* L);

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
