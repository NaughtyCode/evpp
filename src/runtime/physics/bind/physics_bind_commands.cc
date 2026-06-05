#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace engine {
namespace physics_bindings {

namespace {

struct Point3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

constexpr size_t kMaxLuaContactPoints = 1024;
constexpr size_t kMaxLuaDiffValues = 16;

uint16_t CheckUInt16(lua_State* L, int index, const char* name) {
	lua_Integer value = luaL_checkinteger(L, index);
	luaL_argcheck(L,
				  value >= 0 &&
					  static_cast<uint64_t>(value) <=
						  static_cast<uint64_t>((std::numeric_limits<uint16_t>::max)()),
				  index,
				  name);
	return static_cast<uint16_t>(value);
}

JPH::Real OptionalReal(lua_State* L, int index, JPH::Real fallback, const char* name) {
	return lua_gettop(L) >= index && !lua_isnil(L, index) ? CheckFiniteReal(L, index, name)
														  : fallback;
}

float OptionalFloat(lua_State* L, int index, float fallback, const char* name) {
	return lua_gettop(L) >= index && !lua_isnil(L, index) ? CheckFiniteFloat(L, index, name)
														  : fallback;
}

uint64_t OptionalUInt64(lua_State* L, int index, uint64_t fallback, const char* name) {
	return lua_gettop(L) >= index && !lua_isnil(L, index) ? CheckUInt64(L, index, name)
														  : fallback;
}

size_t ExpectedDiffValueCount(uint16_t change_mask) {
	size_t count = 0;
	if (change_mask & (1 << 0)) count += 3;
	if (change_mask & (1 << 1)) count += 4;
	if (change_mask & (1 << 2)) count += 3;
	if (change_mask & (1 << 3)) count += 3;
	return count;
}

const char* CommandTypeName(CommandType type) {
	switch (type) {
		case CommandType::Spawn:
			return "spawn";
		case CommandType::Destroy:
			return "destroy";
		case CommandType::ApplyForce:
			return "apply_force";
		case CommandType::SetVelocity:
			return "set_velocity";
		case CommandType::Tick:
			return "tick";
		default:
			return "unknown";
	}
}

const char* CollisionTypeName(CollisionEvent::Type type) {
	switch (type) {
		case CollisionEvent::Type::Start:
			return "start";
		case CollisionEvent::Type::Persist:
			return "persist";
		case CollisionEvent::Type::End:
			return "end";
		default:
			return "unknown";
	}
}

CollisionEvent::Type CheckCollisionType(lua_State* L, int index) {
	if (lua_isinteger(L, index)) {
		lua_Integer value = lua_tointeger(L, index);
		luaL_argcheck(L, value >= 0 && value <= 2, index, "collision type out of range");
		return static_cast<CollisionEvent::Type>(value);
	}
	const char* name = luaL_checkstring(L, index);
	std::string type(name);
	if (type == "start") return CollisionEvent::Type::Start;
	if (type == "persist") return CollisionEvent::Type::Persist;
	if (type == "end") return CollisionEvent::Type::End;
	luaL_argerror(L, index, "collision type must be start, persist, or end");
	return CollisionEvent::Type::Start;
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

void PushCommandHeader(lua_State* L, CommandType type) {
	SetField(L, "type", CommandTypeName(type));
	SetField(L, "command_type", CommandTypeName(type));
	SetField(L, "commandType", CommandTypeName(type));
}

void PushBodyTransform(lua_State* L, const BodyTransform& transform) {
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

void PushCollisionEvent(lua_State* L, uint32_t body_a, uint32_t body_b,
						CollisionEvent::Type type, const std::vector<Point3>& points) {
	lua_newtable(L);
	SetField(L, "body_a", body_a);
	SetField(L, "bodyA", body_a);
	SetField(L, "body_b", body_b);
	SetField(L, "bodyB", body_b);
	SetField(L, "type", CollisionTypeName(type));

	lua_newtable(L);
	for (size_t i = 0; i < points.size(); ++i) {
		PushVec3(L, points[i].x, points[i].y, points[i].z);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "contact_points");
	lua_getfield(L, -1, "contact_points");
	lua_setfield(L, -2, "contactPoints");
	lua_getfield(L, -1, "contact_points");
	lua_setfield(L, -2, "points");
}

void PushDiffPacket(lua_State* L, const DiffPacket& packet) {
	lua_newtable(L);
	SetField(L, "object_id", packet.object_id);
	SetField(L, "objectId", packet.object_id);
	SetField(L, "change_mask", static_cast<uint32_t>(packet.change_mask));
	SetField(L, "changeMask", static_cast<uint32_t>(packet.change_mask));
	lua_newtable(L);
	for (size_t i = 0; i < packet.values.size(); ++i) {
		lua_pushnumber(L, packet.values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "values");
}

bool ReadPoint(lua_State* L, int index, Point3& point) {
	index = lua_absindex(L, index);
	if (!lua_istable(L, index)) return false;

	lua_rawgeti(L, index, 1);
	lua_rawgeti(L, index, 2);
	lua_rawgeti(L, index, 3);
	if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
		point.x = lua_tonumber(L, -3);
		point.y = lua_tonumber(L, -2);
		point.z = lua_tonumber(L, -1);
		lua_pop(L, 3);
		return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
	}
	lua_pop(L, 3);

	lua_getfield(L, index, "x");
	lua_getfield(L, index, "y");
	lua_getfield(L, index, "z");
	if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
		lua_pop(L, 3);
		return false;
	}
	point.x = lua_tonumber(L, -3);
	point.y = lua_tonumber(L, -2);
	point.z = lua_tonumber(L, -1);
	lua_pop(L, 3);
	return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

std::vector<Point3> ReadContactPoints(lua_State* L, int index) {
	std::vector<Point3> points;
	if (lua_gettop(L) < index || lua_isnil(L, index)) return points;
	luaL_checktype(L, index, LUA_TTABLE);
	index = lua_absindex(L, index);
	const size_t len = lua_rawlen(L, index);
	luaL_argcheck(L,
				  len <= kMaxLuaContactPoints,
				  index,
				  "too many contact points");
	points.reserve(len);
	for (size_t i = 1; i <= len; ++i) {
		lua_rawgeti(L, index, static_cast<lua_Integer>(i));
		Point3 point;
		if (!ReadPoint(L, -1, point)) {
			lua_pop(L, 1);
			luaL_argerror(L, index, "contact point entries must be finite vec3 tables");
			return {};
		}
		lua_pop(L, 1);
		points.push_back(point);
	}
	return points;
}

std::vector<float> ReadFloatValues(lua_State* L, int index) {
	luaL_checktype(L, index, LUA_TTABLE);
	index = lua_absindex(L, index);
	const size_t len = lua_rawlen(L, index);
	luaL_argcheck(L, len <= kMaxLuaDiffValues, index, "too many diff values");
	std::vector<float> values;
	values.reserve(len);
	for (size_t i = 1; i <= len; ++i) {
		lua_rawgeti(L, index, static_cast<lua_Integer>(i));
		if (!lua_isnumber(L, -1)) {
			lua_pop(L, 1);
			luaL_argerror(L, index, "values entries must be numbers");
			return {};
		}
		double value = lua_tonumber(L, -1);
		lua_pop(L, 1);
		if (!std::isfinite(value) ||
			value < -static_cast<double>((std::numeric_limits<float>::max)()) ||
			value > static_cast<double>((std::numeric_limits<float>::max)())) {
			luaL_argerror(L, index, "values entries must fit float");
			return {};
		}
		values.push_back(static_cast<float>(value));
	}
	return values;
}

int LuaMakeSpawnArgs(lua_State* L) {
	const char* proto_id = luaL_checkstring(L, 1);
	JPH::Real x = OptionalReal(L, 2, 0.0, "x must fit JPH::Real");
	JPH::Real y = OptionalReal(L, 3, 0.0, "y must fit JPH::Real");
	JPH::Real z = OptionalReal(L, 4, 0.0, "z must fit JPH::Real");
	float qx = OptionalFloat(L, 5, 0.0f, "qx must be finite");
	float qy = OptionalFloat(L, 6, 0.0f, "qy must be finite");
	float qz = OptionalFloat(L, 7, 0.0f, "qz must be finite");
	float qw = OptionalFloat(L, 8, 1.0f, "qw must be finite");
	uint64_t user_data = OptionalUInt64(L, 9, 0, "user_data must be >= 0");
	auto rotation = NormalizedOrIdentity(qx, qy, qz, qw);

	lua_newtable(L);
	PushCommandHeader(L, CommandType::Spawn);
	SetField(L, "proto_id", proto_id);
	SetField(L, "protoId", proto_id);
	PushVec3(L, x, y, z);
	lua_setfield(L, -2, "position");
	PushQuat(L, rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW());
	lua_setfield(L, -2, "rotation");
	SetField(L, "user_data", user_data);
	SetField(L, "userData", user_data);
	return 1;
}

int LuaMakeDestroyArgs(lua_State* L) {
	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	lua_newtable(L);
	PushCommandHeader(L, CommandType::Destroy);
	SetField(L, "body_id", body_id);
	SetField(L, "bodyId", body_id);
	return 1;
}

int LuaMakeApplyForceArgs(lua_State* L) {
	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	float fx = CheckFiniteFloat(L, 2, "fx must be finite");
	float fy = CheckFiniteFloat(L, 3, "fy must be finite");
	float fz = CheckFiniteFloat(L, 4, "fz must be finite");
	JPH::Real px = CheckFiniteReal(L, 5, "px must fit JPH::Real");
	JPH::Real py = CheckFiniteReal(L, 6, "py must fit JPH::Real");
	JPH::Real pz = CheckFiniteReal(L, 7, "pz must fit JPH::Real");

	lua_newtable(L);
	PushCommandHeader(L, CommandType::ApplyForce);
	SetField(L, "body_id", body_id);
	SetField(L, "bodyId", body_id);
	PushVec3(L, fx, fy, fz);
	lua_setfield(L, -2, "force");
	PushVec3(L, px, py, pz);
	lua_setfield(L, -2, "point");
	return 1;
}

int LuaMakeSetVelocityArgs(lua_State* L) {
	uint32_t body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	float vx = CheckFiniteFloat(L, 2, "vx must be finite");
	float vy = CheckFiniteFloat(L, 3, "vy must be finite");
	float vz = CheckFiniteFloat(L, 4, "vz must be finite");

	lua_newtable(L);
	PushCommandHeader(L, CommandType::SetVelocity);
	SetField(L, "body_id", body_id);
	SetField(L, "bodyId", body_id);
	PushVec3(L, vx, vy, vz);
	lua_setfield(L, -2, "velocity");
	return 1;
}

int LuaMakeTickArgs(lua_State* L) {
	uint64_t frame_id = CheckUInt64(L, 1, "frame_id must be >= 0");
	float delta_time = CheckFiniteFloat(L, 2, "delta_time must be finite");
	luaL_argcheck(L, delta_time > 0.0f, 2, "delta_time must be > 0");

	lua_newtable(L);
	PushCommandHeader(L, CommandType::Tick);
	SetField(L, "frame_id", frame_id);
	SetField(L, "frameId", frame_id);
	SetField(L, "delta_time", delta_time);
	SetField(L, "deltaTime", delta_time);
	return 1;
}

int LuaMakeBodyTransform(lua_State* L) {
	BodyTransform transform;
	transform.body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	transform.pos_x = CheckFiniteReal(L, 2, "pos_x must fit JPH::Real");
	transform.pos_y = CheckFiniteReal(L, 3, "pos_y must fit JPH::Real");
	transform.pos_z = CheckFiniteReal(L, 4, "pos_z must fit JPH::Real");
	transform.rot_x = CheckFiniteFloat(L, 5, "rot_x must be finite");
	transform.rot_y = CheckFiniteFloat(L, 6, "rot_y must be finite");
	transform.rot_z = CheckFiniteFloat(L, 7, "rot_z must be finite");
	transform.rot_w = CheckFiniteFloat(L, 8, "rot_w must be finite");
	PushBodyTransform(L, transform);
	return 1;
}

int LuaMakeCollisionEvent(lua_State* L) {
	uint32_t body_a = CheckUInt32(L, 1, "body_a must be a uint32");
	uint32_t body_b = CheckUInt32(L, 2, "body_b must be a uint32");
	auto type = CheckCollisionType(L, 3);
	auto points = ReadContactPoints(L, 4);
	PushCollisionEvent(L, body_a, body_b, type, points);
	return 1;
}

int LuaMakeDiffPacket(lua_State* L) {
	DiffPacket packet;
	packet.object_id = CheckUInt32(L, 1, "object_id must be a uint32");
	packet.change_mask = CheckUInt16(L, 2, "change_mask must be a uint16");
	luaL_argcheck(L, (packet.change_mask & ~0x0Fu) == 0, 2, "change_mask uses reserved bits");
	packet.values = ReadFloatValues(L, 3);
	luaL_argcheck(L,
				  packet.values.size() == ExpectedDiffValueCount(packet.change_mask),
				  3,
				  "values length does not match change_mask");
	PushDiffPacket(L, packet);
	return 1;
}

int LuaMakeFrameResult(lua_State* L) {
	uint64_t frame_id = CheckUInt64(L, 1, "frame_id must be >= 0");
	const char* error = lua_gettop(L) >= 2 && !lua_isnil(L, 2) ? luaL_checkstring(L, 2) : "";
	lua_newtable(L);
	SetField(L, "frame_id", frame_id);
	SetField(L, "frameId", frame_id);
	SetField(L, "error", error);
	lua_newtable(L);
	lua_setfield(L, -2, "transforms");
	lua_newtable(L);
	lua_setfield(L, -2, "collision_events");
	lua_getfield(L, -1, "collision_events");
	lua_setfield(L, -2, "collisionEvents");
	lua_newtable(L);
	lua_setfield(L, -2, "diff_packets");
	lua_getfield(L, -1, "diff_packets");
	lua_setfield(L, -2, "diffPackets");
	return 1;
}

int LuaCommandTypeName(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	std::string type(name);
	if (type == "spawn" || type == "destroy" || type == "apply_force" ||
		type == "set_velocity" || type == "tick") {
		lua_pushstring(L, name);
		return 1;
	}
	return PushNilError(L, "unknown command type");
}

int LuaCollisionTypeName(lua_State* L) {
	auto type = CheckCollisionType(L, 1);
	lua_pushstring(L, CollisionTypeName(type));
	return 1;
}

const luaL_Reg kCommandFunctions[] = {{"make_spawn_args", LuaMakeSpawnArgs},
									  {"make_destroy_args", LuaMakeDestroyArgs},
									  {"make_apply_force_args", LuaMakeApplyForceArgs},
									  {"make_set_velocity_args", LuaMakeSetVelocityArgs},
									  {"make_tick_args", LuaMakeTickArgs},
									  {"make_body_transform", LuaMakeBodyTransform},
									  {"make_collision_event", LuaMakeCollisionEvent},
									  {"make_diff_packet", LuaMakeDiffPacket},
									  {"make_frame_result", LuaMakeFrameResult},
									  {"command_type_name", LuaCommandTypeName},
									  {"collision_type_name", LuaCollisionTypeName},
									  {nullptr, nullptr}};

}  // namespace

void RegisterCommandBindings(lua_State* L) {
	luaL_setfuncs(L, kCommandFunctions, 0);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
