#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <cmath>
#include <limits>
#include <string>

#include <Jolt/Physics/Body/MotionQuality.h>
#include <Jolt/Physics/Body/MotionType.h>

#include "runtime/physics/physics_body_assets.h"
#include "runtime/physics/physics_diff.h"

namespace engine {
namespace physics_bindings {

namespace {

const char* MotionTypeName(JPH::EMotionType type) {
	switch (type) {
		case JPH::EMotionType::Static:
			return "static";
		case JPH::EMotionType::Kinematic:
			return "kinematic";
		case JPH::EMotionType::Dynamic:
			return "dynamic";
		default:
			return "unknown";
	}
}

const char* MotionQualityName(JPH::EMotionQuality quality) {
	switch (quality) {
		case JPH::EMotionQuality::Discrete:
			return "discrete";
		case JPH::EMotionQuality::LinearCast:
			return "linear_cast";
		default:
			return "unknown";
	}
}

PhysicsWorld::Stats GetStatsFromContext(const BindingContext& ctx) {
	if (ctx.IsPhysicsThread() && ctx.world) {
		return ctx.world->GetStats();
	}
	if (!ctx.system) {
		return {};
	}
	auto s = ctx.system->GetPhysicsStats();
	return {s.active_bodies, s.total_bodies, s.body_pairs, s.contact_constraints};
}

bool CheckWorldDirectRead(lua_State* L, const BindingContext& ctx) {
	if (!ctx.world) {
		PushNilError(L, "physics world not available");
		return false;
	}
	if (ctx.system && ctx.system->IsRunning() && !ctx.IsPhysicsThread()) {
		PushNilError(L, "physics world direct read is only available on the physics thread");
		return false;
	}
	return true;
}

void PushPrototype(lua_State* L, const PrototypeEntry& proto) {
	lua_newtable(L);
	SetField(L, "proto_id", proto.proto_id);
	SetField(L, "mass", proto.mass);
	SetField(L, "friction", proto.friction);
	SetField(L, "restitution", proto.restitution);
	SetField(L, "motion_type", MotionTypeName(proto.motion_type));
	SetField(L, "motion_quality", MotionQualityName(proto.motion_quality));
	SetField(L, "linear_damping", proto.linear_damping);
	SetField(L, "angular_damping", proto.angular_damping);
	SetField(L, "gravity_factor", proto.gravity_factor);
	SetField(L, "object_layer", static_cast<uint32_t>(proto.object_layer));
	SetField(L, "allowed_dofs", static_cast<uint32_t>(proto.allowed_dofs));
	SetField(L, "is_sensor", proto.is_sensor);
	SetField(L, "allow_sleeping", proto.allow_sleeping);
	SetField(L, "max_linear_velocity", proto.max_linear_velocity);
	SetField(L, "max_angular_velocity", proto.max_angular_velocity);
}

void PushBodyTransform(lua_State* L, const BodyTransform& transform) {
	lua_newtable(L);
	SetField(L, "body_id", transform.body_id);
	SetField(L, "pos_x", transform.pos_x);
	SetField(L, "pos_y", transform.pos_y);
	SetField(L, "pos_z", transform.pos_z);
	SetField(L, "rot_x", transform.rot_x);
	SetField(L, "rot_y", transform.rot_y);
	SetField(L, "rot_z", transform.rot_z);
	SetField(L, "rot_w", transform.rot_w);
}

void PushCollisionEvent(lua_State* L, const CollisionEvent& event) {
	lua_newtable(L);
	SetField(L, "body_a", event.body_a);
	SetField(L, "body_b", event.body_b);
	const char* type = "start";
	if (event.type == CollisionEvent::Type::Persist) {
		type = "persist";
	} else if (event.type == CollisionEvent::Type::End) {
		type = "end";
	}
	SetField(L, "type", type);

	lua_newtable(L);
	for (size_t i = 0; i < event.contact_points.size(); ++i) {
		const auto& point = event.contact_points[i];
		lua_newtable(L);
		SetField(L, "x", point.GetX());
		SetField(L, "y", point.GetY());
		SetField(L, "z", point.GetZ());
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "contact_points");
}

void PushDiffPacket(lua_State* L, const DiffPacket& packet) {
	lua_newtable(L);
	SetField(L, "object_id", packet.object_id);
	SetField(L, "change_mask", static_cast<uint32_t>(packet.change_mask));
	lua_newtable(L);
	for (size_t i = 0; i < packet.values.size(); ++i) {
		lua_pushnumber(L, packet.values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "values");
}

bool ReadVec3Like(lua_State* L, int index, double& x, double& y, double& z) {
	index = lua_absindex(L, index);
	if (!lua_istable(L, index)) return false;
	lua_rawgeti(L, index, 1);
	lua_rawgeti(L, index, 2);
	lua_rawgeti(L, index, 3);
	if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
		x = lua_tonumber(L, -3);
		y = lua_tonumber(L, -2);
		z = lua_tonumber(L, -1);
		lua_pop(L, 3);
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}
	lua_pop(L, 3);

	lua_getfield(L, index, "x");
	lua_getfield(L, index, "y");
	lua_getfield(L, index, "z");
	if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
		lua_pop(L, 3);
		return false;
	}
	x = lua_tonumber(L, -3);
	y = lua_tonumber(L, -2);
	z = lua_tonumber(L, -1);
	lua_pop(L, 3);
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

bool FitsFloat(double value);

bool ReadQuatLike(lua_State* L, int index, float& x, float& y, float& z, float& w) {
	index = lua_absindex(L, index);
	if (!lua_istable(L, index)) return false;
	lua_rawgeti(L, index, 1);
	lua_rawgeti(L, index, 2);
	lua_rawgeti(L, index, 3);
	lua_rawgeti(L, index, 4);
	if (lua_isnumber(L, -4) && lua_isnumber(L, -3) && lua_isnumber(L, -2) &&
		lua_isnumber(L, -1)) {
		double dx = lua_tonumber(L, -4);
		double dy = lua_tonumber(L, -3);
		double dz = lua_tonumber(L, -2);
		double dw = lua_tonumber(L, -1);
		lua_pop(L, 4);
		if (!FitsFloat(dx) || !FitsFloat(dy) || !FitsFloat(dz) || !FitsFloat(dw)) {
			return false;
		}
		x = static_cast<float>(dx);
		y = static_cast<float>(dy);
		z = static_cast<float>(dz);
		w = static_cast<float>(dw);
		return true;
	}
	lua_pop(L, 4);

	lua_getfield(L, index, "x");
	lua_getfield(L, index, "y");
	lua_getfield(L, index, "z");
	lua_getfield(L, index, "w");
	if (!lua_isnumber(L, -4) || !lua_isnumber(L, -3) || !lua_isnumber(L, -2) ||
		!lua_isnumber(L, -1)) {
		lua_pop(L, 4);
		return false;
	}
	double dx = lua_tonumber(L, -4);
	double dy = lua_tonumber(L, -3);
	double dz = lua_tonumber(L, -2);
	double dw = lua_tonumber(L, -1);
	lua_pop(L, 4);
	if (!FitsFloat(dx) || !FitsFloat(dy) || !FitsFloat(dz) || !FitsFloat(dw)) {
		return false;
	}
	x = static_cast<float>(dx);
	y = static_cast<float>(dy);
	z = static_cast<float>(dz);
	w = static_cast<float>(dw);
	return true;
}

bool ReadStateVec3Field(lua_State* L,
						int table_index,
						const char* name,
						const char* fallback_name,
						double& x,
						double& y,
						double& z) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, name);
	if (!lua_istable(L, -1) && fallback_name) {
		lua_pop(L, 1);
		lua_getfield(L, table_index, fallback_name);
	}
	bool ok = ReadVec3Like(L, -1, x, y, z);
	lua_pop(L, 1);
	return ok;
}

bool ReadStateQuatField(lua_State* L,
						int table_index,
						const char* name,
						float& x,
						float& y,
						float& z,
						float& w) {
	table_index = lua_absindex(L, table_index);
	lua_getfield(L, table_index, name);
	bool ok = ReadQuatLike(L, -1, x, y, z, w);
	lua_pop(L, 1);
	return ok;
}

bool FitsReal(double value) {
	return std::isfinite(value) &&
		   std::abs(value) <= static_cast<double>((std::numeric_limits<JPH::Real>::max)());
}

bool FitsFloat(double value) {
	return std::isfinite(value) &&
		   value >= -static_cast<double>((std::numeric_limits<float>::max)()) &&
		   value <= static_cast<double>((std::numeric_limits<float>::max)());
}

bool ReadSnapshot(lua_State* L, int index, BodyStateSnapshot& out) {
	index = lua_absindex(L, index);
	double px = 0.0;
	double py = 0.0;
	double pz = 0.0;
	if (!ReadStateVec3Field(L, index, "position", nullptr, px, py, pz)) return false;
	if (!FitsReal(px) || !FitsReal(py) || !FitsReal(pz)) return false;
	out.position = JPH::RVec3(
		static_cast<JPH::Real>(px), static_cast<JPH::Real>(py), static_cast<JPH::Real>(pz));

	float qx = 0.0f;
	float qy = 0.0f;
	float qz = 0.0f;
	float qw = 1.0f;
	if (!ReadStateQuatField(L, index, "rotation", qx, qy, qz, qw)) return false;
	out.rotation = NormalizedOrIdentity(qx, qy, qz, qw);

	double lvx = 0.0;
	double lvy = 0.0;
	double lvz = 0.0;
	if (!ReadStateVec3Field(L, index, "linear_velocity", "linearVelocity", lvx, lvy, lvz)) {
		return false;
	}
	if (!FitsFloat(lvx) || !FitsFloat(lvy) || !FitsFloat(lvz)) return false;
	out.linear_velocity = JPH::Vec3(
		static_cast<float>(lvx), static_cast<float>(lvy), static_cast<float>(lvz));

	double avx = 0.0;
	double avy = 0.0;
	double avz = 0.0;
	if (!ReadStateVec3Field(L, index, "angular_velocity", "angularVelocity", avx, avy, avz)) {
		return false;
	}
	if (!FitsFloat(avx) || !FitsFloat(avy) || !FitsFloat(avz)) return false;
	out.angular_velocity = JPH::Vec3(
		static_cast<float>(avx), static_cast<float>(avy), static_cast<float>(avz));
	return true;
}

ThresholdsConfig ReadThresholds(lua_State* L, int index, ThresholdsConfig defaults) {
	if (!lua_istable(L, index)) return defaults;
	index = lua_absindex(L, index);
	auto read_field = [&](const char* snake, const char* camel, float& out) {
		lua_getfield(L, index, snake);
		if (!lua_isnumber(L, -1) && camel) {
			lua_pop(L, 1);
			lua_getfield(L, index, camel);
		}
		if (lua_isnumber(L, -1)) {
			double value = lua_tonumber(L, -1);
			if (FitsFloat(value) && value >= 0.0) {
				out = static_cast<float>(value);
			}
		}
		lua_pop(L, 1);
	};
	read_field("position_epsilon", "positionEpsilon", defaults.position_epsilon);
	read_field("rotation_epsilon", "rotationEpsilon", defaults.rotation_epsilon);
	read_field("linear_velocity_epsilon",
			   "linearVelocityEpsilon",
			   defaults.linear_velocity_epsilon);
	read_field("angular_velocity_epsilon",
			   "angularVelocityEpsilon",
			   defaults.angular_velocity_epsilon);
	return defaults;
}

void PushFrameResult(lua_State* L, const PhysicsFrameResult& result) {
	lua_newtable(L);
	SetField(L, "frame_id", result.frame_id);
	SetField(L, "error", result.error);

	lua_newtable(L);
	for (size_t i = 0; i < result.transforms.size(); ++i) {
		PushBodyTransform(L, result.transforms[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "transforms");

	lua_newtable(L);
	for (size_t i = 0; i < result.collision_events.size(); ++i) {
		PushCollisionEvent(L, result.collision_events[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "collision_events");

	lua_newtable(L);
	for (size_t i = 0; i < result.diff_packets.size(); ++i) {
		PushDiffPacket(L, result.diff_packets[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "diff_packets");
}

int LuaTick(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (ctx.IsPhysicsThread()) {
		return PushNilError(L, "tick is unavailable on the physics thread");
	}

	uint64_t frame_id = CheckUInt64(L, 1, "frame_id must be >= 0");
	float delta_time = CheckFiniteFloat(L, 2, "delta_time must be finite");
	bool ok = ctx.system->Tick(frame_id, delta_time);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaFetchResult(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (ctx.IsPhysicsThread()) {
		return PushNilError(L, "fetch_result is unavailable on the physics thread");
	}

	uint64_t frame_id = CheckUInt64(L, 1, "frame_id must be >= 0");
	int timeout_ms = static_cast<int>(luaL_checkinteger(L, 2));
	luaL_argcheck(L, timeout_ms >= 0, 2, "timeout_ms must be >= 0");

	auto result = ctx.system->FetchResult(frame_id, timeout_ms);
	if (!result.has_value()) {
		lua_pushnil(L);
		return 1;
	}
	PushFrameResult(L, *result);
	return 1;
}

int LuaSaveState(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	std::string data;
	if (ctx.IsPhysicsThread()) {
		if (!CheckWorld(L, ctx)) return 2;
		data = ctx.world->SaveState();
	} else {
		data = ctx.system->SaveState();
	}
	if (data.empty()) {
		return PushNilError(L, "save_state failed");
	}
	lua_pushlstring(L, data.data(), data.size());
	return 1;
}

int LuaRestoreState(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	size_t len = 0;
	const char* data = luaL_checklstring(L, 1, &len);
	bool ok = false;
	if (ctx.IsPhysicsThread()) {
		ok = ctx.world && ctx.world->RestoreState(std::string(data, len));
	} else {
		ok = ctx.system->RestoreState(std::string(data, len));
	}
	if (!ok) {
		return PushNilError(L, "restore_state failed");
	}
	lua_pushboolean(L, 1);
	return 1;
}

int LuaRecover(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (ctx.IsPhysicsThread()) {
		return PushNilError(L, "recover is unavailable on the physics thread");
	}

	std::string saved_state;
	if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
		size_t len = 0;
		const char* data = lua_tolstring(L, 1, &len);
		saved_state.assign(data, len);
	}

	bool ok = ctx.system->Recover(saved_state);
	if (!ok) {
		return PushNilError(L, "recovery failed");
	}
	lua_pushboolean(L, 1);
	return 1;
}

int LuaGetStats(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;

	auto stats = GetStatsFromContext(ctx);
	lua_newtable(L);
	SetField(L, "bodies", stats.total_bodies);
	SetField(L, "active", stats.active_bodies);
	SetField(L, "body_pairs", stats.body_pairs);
	SetField(L, "collisions", stats.contact_constraints);
	SetField(L, "contact_constraints", stats.contact_constraints);
	return 1;
}

int LuaListPrototypes(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	lua_newtable(L);
	int index = 1;
	for (const auto& [_, proto] : ctx.world->GetPrototypes()) {
		PushPrototype(L, proto);
		lua_rawseti(L, -2, index++);
	}
	return 1;
}

int LuaGetPrototype(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	const char* proto_id = luaL_checkstring(L, 1);
	const auto& prototypes = ctx.world->GetPrototypes();
	auto it = prototypes.find(proto_id);
	if (it == prototypes.end()) {
		lua_pushnil(L);
		return 1;
	}
	PushPrototype(L, it->second);
	return 1;
}

int LuaGetRegistrySize(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	lua_pushinteger(L, static_cast<lua_Integer>(ctx.world->GetRegistry().Size()));
	return 1;
}

int LuaHasBodyId(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	lua_pushboolean(L, ctx.world->GetRegistry().Has(body_id) ? 1 : 0);
	return 1;
}

int LuaGetAssetName(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	const std::string& asset_name = ctx.world->GetRegistry().GetAssetName(body_id);
	if (asset_name.empty()) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, asset_name.data(), asset_name.size());
	return 1;
}

int LuaGetBodyId(lua_State* L) {
	BindingContext ctx;
	if (!CheckInit(L, ctx)) return 2;
	if (!CheckWorldDirectRead(L, ctx)) return 2;

	const char* asset_name = luaL_checkstring(L, 1);
	auto body_id = ctx.world->GetRegistry().GetBodyId(asset_name);
	if (!body_id.has_value()) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, static_cast<lua_Integer>(*body_id));
	return 1;
}

int LuaGenerateDiff(lua_State* L) {
	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	BodyStateSnapshot current;
	BodyStateSnapshot previous;
	if (!ReadSnapshot(L, 2, current)) {
		return PushNilError(L, "current state must contain position, rotation, linear_velocity, angular_velocity");
	}
	if (!ReadSnapshot(L, 3, previous)) {
		return PushNilError(L, "previous state must contain position, rotation, linear_velocity, angular_velocity");
	}

	ThresholdsConfig thresholds;
	BindingContext ctx = GetContext(L);
	if (ctx.system) {
		if (auto snapshot = ctx.system->GetThresholdsConfigSnapshot()) {
			thresholds = *snapshot;
		}
	}
	if (lua_gettop(L) >= 4 && lua_istable(L, 4)) {
		thresholds = ReadThresholds(L, 4, thresholds);
	}

	auto diff = GenerateDiff(body_id, current, previous, thresholds);
	if (!diff.has_value()) {
		lua_pushnil(L);
		return 1;
	}
	PushDiffPacket(L, *diff);
	return 1;
}

const luaL_Reg kStateFunctions[] = {{"tick", LuaTick},
									{"fetch_result", LuaFetchResult},
									{"save_state", LuaSaveState},
									{"restore_state", LuaRestoreState},
									{"recover", LuaRecover},
									{"get_stats", LuaGetStats},
									{"get_physics_stats", LuaGetStats},
									{"list_prototypes", LuaListPrototypes},
									{"get_prototype", LuaGetPrototype},
									{"get_registry_size", LuaGetRegistrySize},
									{"has_body_id", LuaHasBodyId},
									{"get_asset_name", LuaGetAssetName},
									{"get_body_id", LuaGetBodyId},
									{"generate_diff", LuaGenerateDiff},
									{nullptr, nullptr}};

}  // namespace

void RegisterStateBindings(lua_State* L) {
	luaL_setfuncs(L, kStateFunctions, 0);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
