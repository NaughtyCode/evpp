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
															   JPH::Real ox,
															   JPH::Real oy,
															   JPH::Real oz,
															   float dx,
															   float dy,
															   float dz,
															   float max_dist) {
	if (ctx.IsPhysicsThread()) {
		if (!ctx.world) return std::nullopt;
		auto hit = ctx.world->RayCast(
			JPH::RVec3(ox, oy, oz),
			JPH::Vec3(dx, dy, dz),
			max_dist);
		if (!hit.has_value()) return std::nullopt;
		return PhysicsSystem::RayCastResult{hit->body_id, hit->x, hit->y, hit->z};
	}
	return ctx.system ? ctx.system->RayCast(ox, oy, oz, dx, dy, dz, max_dist) : std::nullopt;
}

void PushVec3(lua_State* L, double x, double y, double z) {
	lua_newtable(L);
	SetField(L, "x", x);
	SetField(L, "y", y);
	SetField(L, "z", z);
	lua_pushnumber(L, x);
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, y);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, z);
	lua_rawseti(L, -2, 3);
}

void PushQuat(lua_State* L, float x, float y, float z, float w) {
	lua_newtable(L);
	SetField(L, "x", x);
	SetField(L, "y", y);
	SetField(L, "z", z);
	SetField(L, "w", w);
	lua_pushnumber(L, x);
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, y);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, z);
	lua_rawseti(L, -2, 3);
	lua_pushnumber(L, w);
	lua_rawseti(L, -2, 4);
}

void PushTransformTable(lua_State* L, const BodyTransform& transform) {
	lua_newtable(L);
	SetField(L, "body_id", transform.body_id);
	SetField(L, "bodyId", transform.body_id);
	SetField(L, "pos_x", transform.pos_x);
	SetField(L, "posX", transform.pos_x);
	SetField(L, "pos_y", transform.pos_y);
	SetField(L, "posY", transform.pos_y);
	SetField(L, "pos_z", transform.pos_z);
	SetField(L, "posZ", transform.pos_z);
	SetField(L, "rot_x", transform.rot_x);
	SetField(L, "rotX", transform.rot_x);
	SetField(L, "rot_y", transform.rot_y);
	SetField(L, "rotY", transform.rot_y);
	SetField(L, "rot_z", transform.rot_z);
	SetField(L, "rotZ", transform.rot_z);
	SetField(L, "rot_w", transform.rot_w);
	SetField(L, "rotW", transform.rot_w);
	PushVec3(L, transform.pos_x, transform.pos_y, transform.pos_z);
	lua_setfield(L, -2, "position");
	PushQuat(L, transform.rot_x, transform.rot_y, transform.rot_z, transform.rot_w);
	lua_setfield(L, -2, "rotation");
}

void PushVelocityTable(lua_State* L, const PhysicsSystem::Vec3Result& velocity) {
	PushVec3(L, velocity.x, velocity.y, velocity.z);
}

int LuaSpawn(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	const char* proto_id = luaL_checkstring(L, 1);
	JPH::Real x = CheckFiniteReal(L, 2, "x must fit JPH::Real");
	JPH::Real y = CheckFiniteReal(L, 3, "y must fit JPH::Real");
	JPH::Real z = CheckFiniteReal(L, 4, "z must fit JPH::Real");
	float qx = CheckFiniteFloat(L, 5, "qx must be finite");
	float qy = CheckFiniteFloat(L, 6, "qy must be finite");
	float qz = CheckFiniteFloat(L, 7, "qz must be finite");
	float qw = CheckFiniteFloat(L, 8, "qw must be finite");
	uint64_t user_data = lua_gettop(L) >= 9 ? CheckUInt64(L, 9, "user_data must be >= 0") : 0;

	if (ctx.IsPhysicsThread()) {
		if (!CheckWorld(L, ctx)) return 2;
		auto body_id = ctx.world->CreateBody(
			proto_id,
			JPH::RVec3(x, y, z),
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
	JPH::Real px = CheckFiniteReal(L, 5, "px must fit JPH::Real");
	JPH::Real py = CheckFiniteReal(L, 6, "py must fit JPH::Real");
	JPH::Real pz = CheckFiniteReal(L, 7, "pz must fit JPH::Real");

	bool ok = false;
	if (ctx.IsPhysicsThread()) {
		ok = ctx.world &&
			 ctx.world->ApplyForce(body_id,
								   JPH::Vec3(fx, fy, fz),
								   JPH::RVec3(px, py, pz));
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

int LuaGetTransformTable(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	auto t = GetTransformFromContext(ctx, body_id);
	if (!t.has_value()) {
		return PushNilError(L, "body not found");
	}
	PushTransformTable(L, *t);
	return 1;
}

int LuaGetVelocityTable(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	auto v = GetVelocityFromContext(ctx, body_id);
	if (!v.has_value()) {
		return PushNilError(L, "body not found");
	}
	PushVelocityTable(L, *v);
	return 1;
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

int LuaGetBodyState(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	auto transform = GetTransformFromContext(ctx, body_id);
	if (!transform.has_value()) {
		return PushNilError(L, "body not found");
	}

	auto velocity = GetVelocityFromContext(ctx, body_id);
	lua_newtable(L);
	SetField(L, "body_id", body_id);
	SetField(L, "bodyId", body_id);
	PushTransformTable(L, *transform);
	lua_setfield(L, -2, "transform");
	if (velocity.has_value()) {
		PushVelocityTable(L, *velocity);
	} else {
		lua_pushnil(L);
	}
	lua_setfield(L, -2, "velocity");
	bool active = ctx.IsPhysicsThread() ? (ctx.world && ctx.world->IsActive(body_id))
										: ctx.system->IsBodyActive(body_id);
	SetField(L, "active", active);
	return 1;
}

int LuaRayCast(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	JPH::Real ox = CheckFiniteReal(L, 1, "ox must fit JPH::Real");
	JPH::Real oy = CheckFiniteReal(L, 2, "oy must fit JPH::Real");
	JPH::Real oz = CheckFiniteReal(L, 3, "oz must fit JPH::Real");
	float dx = CheckFiniteFloat(L, 4, "dx must be finite");
	float dy = CheckFiniteFloat(L, 5, "dy must be finite");
	float dz = CheckFiniteFloat(L, 6, "dz must be finite");
	float max_dist = CheckFiniteFloat(L, 7, "max_dist must be finite");
	luaL_argcheck(L, max_dist > 0.0f, 7, "max_dist must be > 0");

	auto hit = RayCastFromContext(ctx, ox, oy, oz, dx, dy, dz, max_dist);
	if (!hit.has_value()) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);
	SetField(L, "body_id", hit->body_id);
	SetField(L, "bodyId", hit->body_id);
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
								   {"get_transform_table", LuaGetTransformTable},
								   {"get_velocity_table", LuaGetVelocityTable},
								   {"get_body_state", LuaGetBodyState},
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
