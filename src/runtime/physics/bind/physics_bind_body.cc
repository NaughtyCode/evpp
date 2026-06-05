#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <optional>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>

namespace engine {
namespace physics_bindings {

namespace {

std::optional<BodyTransform> GetTransformFromContext(const BindingContext& ctx,
													  uint32_t body_id) {
	if (ctx.IsPhysicsThread()) {
		if (!ctx.world) return std::nullopt;
		auto result = ctx.world->GetTransform(body_id);
		if (!result.has_value()) return std::nullopt;
		BodyTransform out;
		out.body_id = body_id;
		out.pos_x = result->first.GetX();
		out.pos_y = result->first.GetY();
		out.pos_z = result->first.GetZ();
		out.rot_x = result->second.GetX();
		out.rot_y = result->second.GetY();
		out.rot_z = result->second.GetZ();
		out.rot_w = result->second.GetW();
		return out;
	}
	return ctx.system ? ctx.system->GetTransform(body_id) : std::nullopt;
}

std::optional<PhysicsSystem::Vec3Result> GetVelocityFromContext(const BindingContext& ctx,
																uint32_t body_id) {
	if (ctx.IsPhysicsThread()) {
		if (!ctx.world) return std::nullopt;
		auto result = ctx.world->GetVelocity(body_id);
		if (!result.has_value()) return std::nullopt;
		return PhysicsSystem::Vec3Result{result->GetX(), result->GetY(), result->GetZ()};
	}
	return ctx.system ? ctx.system->GetVelocity(body_id) : std::nullopt;
}

std::optional<PhysicsSystem::RayCastResult> RayCastFromContext(const BindingContext& ctx,
															   double ox,
															   double oy,
															   double oz,
															   double dx,
															   double dy,
															   double dz,
															   float max_dist) {
	if (ctx.IsPhysicsThread()) {
		if (!ctx.world) return std::nullopt;
		auto hit = ctx.world->RayCast(
			JPH::RVec3(
				static_cast<JPH::Real>(ox), static_cast<JPH::Real>(oy), static_cast<JPH::Real>(oz)),
			JPH::Vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)),
			max_dist);
		if (!hit.has_value()) return std::nullopt;
		return PhysicsSystem::RayCastResult{hit->body_id, hit->x, hit->y, hit->z};
	}
	return ctx.system ? ctx.system->RayCast(ox, oy, oz, dx, dy, dz, max_dist) : std::nullopt;
}

int LuaSpawn(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	const char* proto_id = luaL_checkstring(L, 1);
	double x = CheckFiniteDouble(L, 2, "x must be finite");
	double y = CheckFiniteDouble(L, 3, "y must be finite");
	double z = CheckFiniteDouble(L, 4, "z must be finite");
	float qx = CheckFiniteFloat(L, 5, "qx must be finite");
	float qy = CheckFiniteFloat(L, 6, "qy must be finite");
	float qz = CheckFiniteFloat(L, 7, "qz must be finite");
	float qw = CheckFiniteFloat(L, 8, "qw must be finite");
	uint64_t user_data = lua_gettop(L) >= 9 ? CheckUInt64(L, 9, "user_data must be >= 0") : 0;

	if (ctx.IsPhysicsThread()) {
		if (!CheckWorld(L, ctx)) return 2;
		auto body_id = ctx.world->CreateBody(
			proto_id,
			JPH::RVec3(static_cast<JPH::Real>(x),
					   static_cast<JPH::Real>(y),
					   static_cast<JPH::Real>(z)),
			NormalizedOrIdentity(qx, qy, qz, qw),
			user_data);
		if (!body_id.has_value()) {
			return PushNilError(L, "spawn failed");
		}
		lua_pushinteger(L, static_cast<lua_Integer>(*body_id));
		return 1;
	}

	bool accepted = ctx.system->EnqueueSpawn(proto_id, x, y, z, qx, qy, qz, qw, user_data);
	if (!accepted) {
		return PushNilError(L, "spawn command rejected");
	}

	lua_pushinteger(L, 0);
	return 1;
}

int LuaDestroy(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	bool ok = false;
	if (ctx.IsPhysicsThread()) {
		ok = ctx.world && ctx.world->DestroyBody(body_id);
	} else {
		ok = ctx.system->EnqueueDestroy(body_id);
	}

	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaApplyForce(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	float fx = CheckFiniteFloat(L, 2, "fx must be finite");
	float fy = CheckFiniteFloat(L, 3, "fy must be finite");
	float fz = CheckFiniteFloat(L, 4, "fz must be finite");
	double px = CheckFiniteDouble(L, 5, "px must be finite");
	double py = CheckFiniteDouble(L, 6, "py must be finite");
	double pz = CheckFiniteDouble(L, 7, "pz must be finite");

	bool ok = false;
	if (ctx.IsPhysicsThread()) {
		ok = ctx.world &&
			 ctx.world->ApplyForce(body_id,
								   JPH::Vec3(fx, fy, fz),
								   JPH::RVec3(static_cast<JPH::Real>(px),
											  static_cast<JPH::Real>(py),
											  static_cast<JPH::Real>(pz)));
	} else {
		ok = ctx.system->EnqueueApplyForce(body_id, fx, fy, fz, px, py, pz);
	}
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaSetVelocity(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	float vx = CheckFiniteFloat(L, 2, "vx must be finite");
	float vy = CheckFiniteFloat(L, 3, "vy must be finite");
	float vz = CheckFiniteFloat(L, 4, "vz must be finite");

	bool ok = false;
	if (ctx.IsPhysicsThread()) {
		ok = ctx.world && ctx.world->SetVelocity(body_id, JPH::Vec3(vx, vy, vz));
	} else {
		ok = ctx.system->EnqueueSetVelocity(body_id, vx, vy, vz);
	}
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaGetTransform(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	auto t = GetTransformFromContext(ctx, body_id);
	if (!t.has_value()) {
		return PushNilError(L, "body not found");
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

int LuaGetVelocity(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	auto v = GetVelocityFromContext(ctx, body_id);
	if (!v.has_value()) {
		return PushNilError(L, "body not found");
	}
	lua_pushnumber(L, v->x);
	lua_pushnumber(L, v->y);
	lua_pushnumber(L, v->z);
	return 3;
}

int LuaIsActive(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	bool active = ctx.IsPhysicsThread() ? (ctx.world && ctx.world->IsActive(body_id))
										: ctx.system->IsBodyActive(body_id);
	lua_pushboolean(L, active ? 1 : 0);
	return 1;
}

int LuaRayCast(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	double ox = CheckFiniteDouble(L, 1, "ox must be finite");
	double oy = CheckFiniteDouble(L, 2, "oy must be finite");
	double oz = CheckFiniteDouble(L, 3, "oz must be finite");
	double dx = CheckFiniteDouble(L, 4, "dx must be finite");
	double dy = CheckFiniteDouble(L, 5, "dy must be finite");
	double dz = CheckFiniteDouble(L, 6, "dz must be finite");
	float max_dist = CheckFiniteFloat(L, 7, "max_dist must be finite");
	luaL_argcheck(L, max_dist > 0.0f, 7, "max_dist must be > 0");

	auto hit = RayCastFromContext(ctx, ox, oy, oz, dx, dy, dz, max_dist);
	if (!hit.has_value()) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);
	SetField(L, "body_id", hit->body_id);
	SetField(L, "x", hit->x);
	SetField(L, "y", hit->y);
	SetField(L, "z", hit->z);
	return 1;
}

const luaL_Reg kBodyFunctions[] = {{"spawn", LuaSpawn},
								   {"enqueue_spawn", LuaSpawn},
								   {"destroy", LuaDestroy},
								   {"enqueue_destroy", LuaDestroy},
								   {"apply_force", LuaApplyForce},
								   {"enqueue_apply_force", LuaApplyForce},
								   {"set_velocity", LuaSetVelocity},
								   {"enqueue_set_velocity", LuaSetVelocity},
								   {"get_transform", LuaGetTransform},
								   {"get_velocity", LuaGetVelocity},
								   {"is_active", LuaIsActive},
								   {"is_body_active", LuaIsActive},
								   {"ray_cast", LuaRayCast},
								   {nullptr, nullptr}};

}  // namespace

void RegisterBodyBindings(lua_State* L) {
	luaL_setfuncs(L, kBodyFunctions, 0);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
