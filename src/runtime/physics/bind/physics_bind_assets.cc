#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <filesystem>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

#include "runtime/physics/physics_asset_common.h"
#include "runtime/physics/physics_materials.h"
#include "runtime/physics/physics_scene_asset.h"

namespace engine {
namespace physics_bindings {

namespace {

void PushNumberArray(lua_State* L, const std::vector<double>& values) {
	lua_newtable(L);
	for (size_t i = 0; i < values.size(); ++i) {
		lua_pushnumber(L, values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

void PushNumberArray(lua_State* L, const std::vector<float>& values) {
	lua_newtable(L);
	for (size_t i = 0; i < values.size(); ++i) {
		lua_pushnumber(L, values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

void PushUInt8Array(lua_State* L, const std::vector<uint8_t>& values) {
	lua_newtable(L);
	for (size_t i = 0; i < values.size(); ++i) {
		lua_pushinteger(L, static_cast<lua_Integer>(values[i]));
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

void PushOptionalNumberArray(lua_State* L,
							 const char* snake_name,
							 const char* json_name,
							 const std::optional<std::vector<float>>& values) {
	if (!values.has_value()) return;
	PushNumberArray(L, *values);
	lua_setfield(L, -2, snake_name);
	PushNumberArray(L, *values);
	lua_setfield(L, -2, json_name);
}

void PushMaterial(lua_State* L, const JsonMaterial& material) {
	lua_newtable(L);
	SetField(L, "friction", material.friction);
	SetField(L, "restitution", material.restitution);
}

void PushMaterialEntry(lua_State* L, const MaterialEntry& material) {
	lua_newtable(L);
	SetField(L, "name", material.name);
	SetField(L, "friction", material.friction);
	SetField(L, "restitution", material.restitution);
}

void PushTransform(lua_State* L, const JsonTransform& transform) {
	lua_newtable(L);
	PushNumberArray(L, transform.position);
	lua_setfield(L, -2, "position");
	PushNumberArray(L, transform.rotation);
	lua_setfield(L, -2, "rotation");
}

std::string GenericToJson(const glz::generic& value) {
	auto result = glz::write_json(value);
	return result ? *result : "{}";
}

void PushShape(lua_State* L, const JsonShapeDef& shape) {
	lua_newtable(L);
	SetField(L, "type", shape.type);
	SetField(L, "params_json", GenericToJson(shape.params));
	SetField(L, "paramsJson", GenericToJson(shape.params));
	if (shape.material.has_value()) {
		SetField(L, "material", *shape.material);
	}
	if (shape.position.has_value()) {
		PushNumberArray(L, *shape.position);
		lua_setfield(L, -2, "position");
	}
	if (shape.rotation.has_value()) {
		PushNumberArray(L, *shape.rotation);
		lua_setfield(L, -2, "rotation");
	}
	if (shape.shapes.has_value()) {
		lua_newtable(L);
		for (size_t i = 0; i < shape.shapes->size(); ++i) {
			PushShape(L, (*shape.shapes)[i]);
			lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
		}
		lua_setfield(L, -2, "shapes");
	}
}

void PushStaticBody(lua_State* L, const JsonStaticBody& body) {
	lua_newtable(L);
	SetField(L, "id", body.id);
	PushShape(L, body.shape);
	lua_setfield(L, -2, "shape");
	PushMaterial(L, body.material);
	lua_setfield(L, -2, "material");
	PushTransform(L, body.transform);
	lua_setfield(L, -2, "transform");
	SetField(L, "object_layer", body.object_layer);
	SetField(L, "objectLayer", body.object_layer);
}

void PushDynamicPrototype(lua_State* L, const JsonDynamicPrototype& proto) {
	lua_newtable(L);
	SetField(L, "proto_id", proto.proto_id);
	SetField(L, "protoId", proto.proto_id);
	PushShape(L, proto.shape);
	lua_setfield(L, -2, "shape");
	SetField(L, "mass", proto.mass);
	PushMaterial(L, proto.material);
	lua_setfield(L, -2, "material");
	SetField(L, "motion_type", proto.motion_type);
	SetField(L, "motionType", proto.motion_type);
	SetField(L, "motion_quality", proto.motion_quality);
	SetField(L, "motionQuality", proto.motion_quality);
	SetField(L, "linear_damping", proto.linear_damping);
	SetField(L, "linearDamping", proto.linear_damping);
	SetField(L, "angular_damping", proto.angular_damping);
	SetField(L, "angularDamping", proto.angular_damping);
	SetField(L, "gravity_factor", proto.gravity_factor);
	SetField(L, "gravityFactor", proto.gravity_factor);
	SetField(L, "object_layer", proto.object_layer);
	SetField(L, "objectLayer", proto.object_layer);
	PushUInt8Array(L, proto.allowed_dofs);
	lua_setfield(L, -2, "allowed_dofs");
	PushUInt8Array(L, proto.allowed_dofs);
	lua_setfield(L, -2, "allowedDofs");
	SetField(L, "is_sensor", proto.is_sensor);
	SetField(L, "isSensor", proto.is_sensor);
	SetField(L, "allow_sleeping", proto.allow_sleeping);
	SetField(L, "allowSleeping", proto.allow_sleeping);
	SetField(L, "max_linear_velocity", proto.max_linear_velocity);
	SetField(L, "maxLinearVelocity", proto.max_linear_velocity);
	SetField(L, "max_angular_velocity", proto.max_angular_velocity);
	SetField(L, "maxAngularVelocity", proto.max_angular_velocity);
}

void PushDynamicBody(lua_State* L, const JsonDynamicBody& body) {
	lua_newtable(L);
	SetField(L, "id", body.id);
	SetField(L, "proto_id", body.proto_id);
	SetField(L, "protoId", body.proto_id);
	PushTransform(L, body.transform);
	lua_setfield(L, -2, "transform");
	SetField(L, "user_data", body.user_data);
	SetField(L, "userData", body.user_data);
	SetField(L, "activate", body.activate);
	PushOptionalNumberArray(L, "linear_velocity", "linearVelocity", body.linear_velocity);
	PushOptionalNumberArray(L, "angular_velocity", "angularVelocity", body.angular_velocity);
}

void PushConstraint(lua_State* L, const JsonConstraint& constraint) {
	lua_newtable(L);
	SetField(L, "type", constraint.type);
	SetField(L, "body_a", constraint.body_a);
	SetField(L, "bodyA", constraint.body_a);
	SetField(L, "body_b", constraint.body_b);
	SetField(L, "bodyB", constraint.body_b);
	PushNumberArray(L, constraint.pivot);
	lua_setfield(L, -2, "pivot");
	PushNumberArray(L, constraint.axis);
	lua_setfield(L, -2, "axis");
	if (constraint.axis2.has_value()) {
		PushNumberArray(L, *constraint.axis2);
		lua_setfield(L, -2, "axis2");
	}
	lua_newtable(L);
	SetField(L, "min", constraint.limits.min);
	SetField(L, "max", constraint.limits.max);
	lua_setfield(L, -2, "limits");
	lua_newtable(L);
	SetField(L, "frequency", constraint.spring.frequency);
	SetField(L, "damping", constraint.spring.damping);
	lua_setfield(L, -2, "spring");
}

template <typename T, typename PushFn>
void PushArray(lua_State* L, const std::vector<T>& values, PushFn push_fn) {
	lua_newtable(L);
	for (size_t i = 0; i < values.size(); ++i) {
		push_fn(L, values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

void PushScene(lua_State* L, const PhysicsSceneAsset& scene) {
	lua_newtable(L);
	SetField(L, "source_path", scene.source_path);
	SetField(L, "sourcePath", scene.source_path);
	SetField(L, "assets_dir", scene.assets_dir);
	SetField(L, "assetsDir", scene.assets_dir);

	lua_newtable(L);
	SetField(L, "materials", static_cast<uint32_t>(scene.materials.size()));
	SetField(L, "static_bodies", static_cast<uint32_t>(scene.static_bodies.size()));
	SetField(L, "staticBodies", static_cast<uint32_t>(scene.static_bodies.size()));
	SetField(L, "dynamic_prototypes", static_cast<uint32_t>(scene.dynamic_prototypes.size()));
	SetField(L, "dynamicPrototypes", static_cast<uint32_t>(scene.dynamic_prototypes.size()));
	SetField(L, "dynamic_bodies", static_cast<uint32_t>(scene.dynamic_bodies.size()));
	SetField(L, "dynamicBodies", static_cast<uint32_t>(scene.dynamic_bodies.size()));
	SetField(L, "constraints", static_cast<uint32_t>(scene.constraints.size()));
	lua_setfield(L, -2, "counts");

	PushArray(L, scene.materials, PushMaterialEntry);
	lua_setfield(L, -2, "materials");
	PushArray(L, scene.static_bodies, PushStaticBody);
	lua_setfield(L, -2, "static_bodies");
	PushArray(L, scene.static_bodies, PushStaticBody);
	lua_setfield(L, -2, "staticBodies");
	PushArray(L, scene.dynamic_prototypes, PushDynamicPrototype);
	lua_setfield(L, -2, "dynamic_prototypes");
	PushArray(L, scene.dynamic_prototypes, PushDynamicPrototype);
	lua_setfield(L, -2, "dynamicPrototypes");
	PushArray(L, scene.dynamic_bodies, PushDynamicBody);
	lua_setfield(L, -2, "dynamic_bodies");
	PushArray(L, scene.dynamic_bodies, PushDynamicBody);
	lua_setfield(L, -2, "dynamicBodies");
	PushArray(L, scene.constraints, PushConstraint);
	lua_setfield(L, -2, "constraints");
}

bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& target) {
	auto root_it = root.begin();
	auto target_it = target.begin();
	for (; root_it != root.end(); ++root_it, ++target_it) {
		if (target_it == target.end() || *root_it != *target_it) {
			return false;
		}
	}
	return true;
}

std::optional<std::filesystem::path> ResolveResourcePath(const BindingContext& ctx,
														 const std::string& input) {
	if (!ctx.system) return std::nullopt;
	std::filesystem::path config_dir(ctx.system->GetConfigDirSnapshot());
	std::filesystem::path resource_root = config_dir.parent_path().parent_path();
	std::string normalized_input = input;
	std::filesystem::path raw_input(input);
	bool has_drive_prefix = normalized_input.size() >= 2 && normalized_input[1] == ':';
	bool is_unc_path = normalized_input.rfind("\\\\", 0) == 0;
	if (!has_drive_prefix && !is_unc_path && !raw_input.is_absolute()) {
		while (!normalized_input.empty() &&
			   (normalized_input.front() == '/' || normalized_input.front() == '\\')) {
			normalized_input.erase(normalized_input.begin());
		}
	}
	std::filesystem::path requested(normalized_input);
	std::filesystem::path full_path =
		requested.is_absolute() ? requested : (resource_root / requested);

	std::error_code ec;
	auto canonical_root = std::filesystem::weakly_canonical(resource_root, ec);
	if (ec) return std::nullopt;
	ec.clear();
	auto canonical_target = std::filesystem::weakly_canonical(full_path, ec);
	if (ec) return std::nullopt;
	if (!IsPathInside(canonical_root, canonical_target)) {
		return std::nullopt;
	}
	return canonical_target;
}

int PushSceneLoadResult(lua_State* L, const PhysicsSceneAssetLoadResult& result) {
	if (!result.error.empty()) {
		return PushNilError(L, result.error.c_str());
	}
	PushScene(L, result.scene);
	return 1;
}

int LuaLoadConfiguredSceneAsset(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const std::string& path = ctx.system->GetAssetsPathSnapshot();
	if (path.empty()) {
		return PushNilError(L, "configured scene asset path not available");
	}
	return PushSceneLoadResult(L, PhysicsSceneAsset::LoadFromFile(path));
}

int LuaLoadSceneAsset(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const char* path = luaL_checkstring(L, 1);
	auto resolved = ResolveResourcePath(ctx, path);
	if (!resolved.has_value()) {
		return PushNilError(L, "scene asset path must resolve inside the physics resource root");
	}
	return PushSceneLoadResult(L, PhysicsSceneAsset::LoadFromFile(resolved->string()));
}

int LuaParseSceneAssetJson(lua_State* L) {
	size_t len = 0;
	const char* json = luaL_checklstring(L, 1, &len);
	std::string source_path;
	if (lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TSTRING) {
		source_path = lua_tostring(L, 2);
	}
	return PushSceneLoadResult(L, PhysicsSceneAsset::LoadFromJson(std::string(json, len), source_path));
}

int LuaParseMaterialsJson(lua_State* L) {
	size_t len = 0;
	const char* json = luaL_checklstring(L, 1, &len);
	MaterialTable table;
	if (!table.LoadFromJson(std::string(json, len))) {
		return PushNilError(L, "materials json parse failed");
	}

	lua_newtable(L);
	SetField(L, "count", static_cast<uint32_t>(table.Size()));
	lua_newtable(L);
	auto names = table.GetNames();
	for (size_t i = 0; i < names.size(); ++i) {
		lua_pushlstring(L, names[i].data(), names[i].size());
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "names");
	return 1;
}

int LuaIsMotionTypeName(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	lua_pushboolean(L, IsMotionTypeName(name) ? 1 : 0);
	return 1;
}

int LuaIsMotionQualityName(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	lua_pushboolean(L, IsMotionQualityName(name) ? 1 : 0);
	return 1;
}

int LuaBuildAllowedDofs(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	std::vector<uint8_t> dofs;
	const lua_Integer len = static_cast<lua_Integer>(lua_rawlen(L, 1));
	dofs.reserve(static_cast<size_t>(len));
	for (lua_Integer i = 1; i <= len; ++i) {
		lua_rawgeti(L, 1, i);
		if (!lua_isinteger(L, -1)) {
			lua_pop(L, 1);
			return PushNilError(L, "allowed dof entries must be integers");
		}
		lua_Integer value = lua_tointeger(L, -1);
		lua_pop(L, 1);
		if (value < 0 || value > 5) {
			return PushNilError(L, "allowed dof entries must be in range [0, 5]");
		}
		dofs.push_back(static_cast<uint8_t>(value));
	}

	uint8_t mask = 0;
	std::string error;
	if (!BuildAllowedDofs(dofs, mask, error)) {
		return PushNilError(L, error.c_str());
	}
	lua_pushinteger(L, static_cast<lua_Integer>(mask));
	return 1;
}

int LuaResolveObjectLayer(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const char* name = luaL_checkstring(L, 1);
	auto config = ctx.system->GetPhysicsConfigSnapshot();
	if (!config.has_value()) {
		return PushNilError(L, "physics config not available");
	}

	JPH::ObjectLayer layer = 0;
	std::string error;
	if (!ResolveObjectLayer(config->layer_config, name, layer, error)) {
		return PushNilError(L, error.c_str());
	}
	lua_pushinteger(L, static_cast<lua_Integer>(layer));
	return 1;
}

int LuaGetAssetDirectory(lua_State* L) {
	const char* path = luaL_checkstring(L, 1);
	std::string dir = GetAssetDirectory(path);
	lua_pushlstring(L, dir.data(), dir.size());
	return 1;
}

int LuaResolveAssetPath(lua_State* L) {
	const char* assets_dir = luaL_checkstring(L, 1);
	const char* path = luaL_checkstring(L, 2);
	std::string resolved = ResolveAssetPath(assets_dir, path);
	lua_pushlstring(L, resolved.data(), resolved.size());
	return 1;
}

const luaL_Reg kAssetFunctions[] = {{"load_configured_scene_asset", LuaLoadConfiguredSceneAsset},
									{"load_scene_asset", LuaLoadSceneAsset},
									{"parse_scene_asset_json", LuaParseSceneAssetJson},
									{"parse_materials_json", LuaParseMaterialsJson},
									{"is_motion_type_name", LuaIsMotionTypeName},
									{"is_motion_quality_name", LuaIsMotionQualityName},
									{"build_allowed_dofs", LuaBuildAllowedDofs},
									{"resolve_object_layer", LuaResolveObjectLayer},
									{"get_asset_directory", LuaGetAssetDirectory},
									{"resolve_asset_path", LuaResolveAssetPath},
									{nullptr, nullptr}};

}  // namespace

void RegisterAssetBindings(lua_State* L) {
	luaL_setfuncs(L, kAssetFunctions, 0);
}

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
