#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/bind/physics_bind_common.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <glaze/glaze.hpp>

#include "runtime/physics/physics_asset_common.h"
#include "runtime/physics/physics_layers.h"
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

bool ReadNumberArray(lua_State* L,
					 int index,
					 size_t expected,
					 std::vector<double>& out,
					 std::string& error) {
	index = lua_absindex(L, index);
	if (!lua_istable(L, index)) {
		error = "expected number array";
		return false;
	}
	const size_t len = lua_rawlen(L, index);
	if (len != expected) {
		error = "number array has unexpected length";
		return false;
	}
	out.clear();
	out.reserve(expected);
	for (size_t i = 1; i <= expected; ++i) {
		lua_rawgeti(L, index, static_cast<lua_Integer>(i));
		if (!lua_isnumber(L, -1)) {
			lua_pop(L, 1);
			error = "number array entries must be numbers";
			return false;
		}
		double value = lua_tonumber(L, -1);
		lua_pop(L, 1);
		if (!std::isfinite(value)) {
			error = "number array entries must be finite";
			return false;
		}
		out.push_back(value);
	}
	return true;
}

bool ReadFloatArray(lua_State* L,
					int index,
					size_t expected,
					std::vector<float>& out,
					std::string& error) {
	std::vector<double> values;
	if (!ReadNumberArray(L, index, expected, values, error)) return false;
	out.clear();
	out.reserve(expected);
	for (double value : values) {
		if (value < -static_cast<double>((std::numeric_limits<float>::max)()) ||
			value > static_cast<double>((std::numeric_limits<float>::max)())) {
			error = "number array entries exceed float range";
			return false;
		}
		out.push_back(static_cast<float>(value));
	}
	return true;
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

void PushRVec3(lua_State* L, JPH::RVec3Arg value);
void PushVec3(lua_State* L, JPH::Vec3Arg value);
void PushQuat(lua_State* L, JPH::QuatArg value);

void PushSceneBodyRecord(lua_State* L, const PhysicsSceneBodyRecord& record) {
	lua_newtable(L);
	SetField(L, "body_id", record.body_id);
	SetField(L, "bodyId", record.body_id);
	SetField(L, "asset_id", record.asset_id);
	SetField(L, "assetId", record.asset_id);
	PushRVec3(L, record.position);
	lua_setfield(L, -2, "position");
	PushQuat(L, record.rotation);
	lua_setfield(L, -2, "rotation");
	PushVec3(L, record.linear_velocity);
	lua_setfield(L, -2, "linear_velocity");
	PushVec3(L, record.linear_velocity);
	lua_setfield(L, -2, "linearVelocity");
	PushVec3(L, record.angular_velocity);
	lua_setfield(L, -2, "angular_velocity");
	PushVec3(L, record.angular_velocity);
	lua_setfield(L, -2, "angularVelocity");
	SetField(L, "dynamic", record.dynamic);
}

template <typename T, typename PushFn>
void PushArray(lua_State* L, const std::vector<T>& values, PushFn push_fn) {
	lua_newtable(L);
	for (size_t i = 0; i < values.size(); ++i) {
		push_fn(L, values[i]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
}

void PushSceneCounts(lua_State* L, const PhysicsSceneAsset& scene) {
	lua_newtable(L);
	SetField(L, "materials", static_cast<uint32_t>(scene.materials.size()));
	SetField(L, "static_bodies", static_cast<uint32_t>(scene.static_bodies.size()));
	SetField(L, "staticBodies", static_cast<uint32_t>(scene.static_bodies.size()));
	SetField(L, "dynamic_prototypes", static_cast<uint32_t>(scene.dynamic_prototypes.size()));
	SetField(L, "dynamicPrototypes", static_cast<uint32_t>(scene.dynamic_prototypes.size()));
	SetField(L, "dynamic_bodies", static_cast<uint32_t>(scene.dynamic_bodies.size()));
	SetField(L, "dynamicBodies", static_cast<uint32_t>(scene.dynamic_bodies.size()));
	SetField(L, "constraints", static_cast<uint32_t>(scene.constraints.size()));
}

void PushAssetLoadResult(lua_State* L, const AssetLoadResult& result) {
	lua_newtable(L);
	SetField(L, "success", result.success);
	SetField(L, "error", result.error);
	SetField(L, "static_bodies_loaded", result.static_bodies_loaded);
	SetField(L, "staticBodiesLoaded", result.static_bodies_loaded);
	SetField(L, "dynamic_prototypes_loaded", result.dynamic_prototypes_loaded);
	SetField(L, "dynamicPrototypesLoaded", result.dynamic_prototypes_loaded);
	SetField(L, "dynamic_bodies_loaded", result.dynamic_bodies_loaded);
	SetField(L, "dynamicBodiesLoaded", result.dynamic_bodies_loaded);
	SetField(L, "constraints_loaded", result.constraints_loaded);
	SetField(L, "constraintsLoaded", result.constraints_loaded);
	SetField(L, "scene_objects_loaded", result.scene_objects_loaded);
	SetField(L, "sceneObjectsLoaded", result.scene_objects_loaded);

	lua_newtable(L);
	SetField(L, "static_bodies", result.static_bodies_loaded);
	SetField(L, "staticBodies", result.static_bodies_loaded);
	SetField(L, "dynamic_prototypes", result.dynamic_prototypes_loaded);
	SetField(L, "dynamicPrototypes", result.dynamic_prototypes_loaded);
	SetField(L, "dynamic_bodies", result.dynamic_bodies_loaded);
	SetField(L, "dynamicBodies", result.dynamic_bodies_loaded);
	SetField(L, "constraints", result.constraints_loaded);
	SetField(L, "scene_objects", result.scene_objects_loaded);
	SetField(L, "sceneObjects", result.scene_objects_loaded);
	lua_setfield(L, -2, "counts");
}

void PushScene(lua_State* L, const PhysicsSceneAsset& scene) {
	lua_newtable(L);
	SetField(L, "source_path", scene.source_path);
	SetField(L, "sourcePath", scene.source_path);
	SetField(L, "assets_dir", scene.assets_dir);
	SetField(L, "assetsDir", scene.assets_dir);

	PushSceneCounts(L, scene);
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

PhysicsSceneAssetLoadResult ReadSceneJson(lua_State* L, int json_index, int source_index) {
	size_t len = 0;
	const char* json = luaL_checklstring(L, json_index, &len);
	std::string source_path;
	if (lua_gettop(L) >= source_index && lua_type(L, source_index) == LUA_TSTRING) {
		source_path = lua_tostring(L, source_index);
	}
	return PhysicsSceneAsset::LoadFromJson(std::string(json, len), source_path);
}

bool IsReservedConstraintBodyName(const std::string& name) {
	return name == "world" || name == "fixed" || name == "__world__";
}

bool IsFiniteFloatRangeVec(const std::vector<double>& values, size_t expected) {
	if (values.size() != expected) {
		return false;
	}
	constexpr double kMaxFloat = static_cast<double>((std::numeric_limits<float>::max)());
	for (double value : values) {
		if (!std::isfinite(value) || std::abs(value) > kMaxFloat) {
			return false;
		}
	}
	return true;
}

bool ValidateShapeDefBasic(const JsonShapeDef& shape,
						   const std::unordered_set<std::string>& material_names,
						   std::string& error,
						   const std::string& path) {
	if (shape.type.empty() && (!shape.shapes.has_value() || shape.shapes->empty())) {
		error = path + ": shape type must not be empty";
		return false;
	}
	if (shape.material.has_value() && !shape.material->empty() &&
		material_names.find(*shape.material) == material_names.end()) {
		error = path + ": shape references unknown material: " + *shape.material;
		return false;
	}
	if (shape.position.has_value() && !IsFiniteFloatRangeVec(*shape.position, 3)) {
		error = path + ": shape position must be finite float-range vec3";
		return false;
	}
	if (shape.rotation.has_value() && !IsFiniteFloatVec(*shape.rotation, 4)) {
		error = path + ": shape rotation must be finite quat";
		return false;
	}
	if (shape.shapes.has_value()) {
		if (shape.shapes->empty()) {
			error = path + ": compound shape must contain sub-shapes";
			return false;
		}
		for (size_t i = 0; i < shape.shapes->size(); ++i) {
			if (!ValidateShapeDefBasic((*shape.shapes)[i],
									   material_names,
									   error,
									   path + ".shapes[" + std::to_string(i + 1) + "]")) {
				return false;
			}
		}
	}
	return true;
}

bool ValidateObjectLayerName(const PhysicsConfig* config,
							 const std::string& name,
							 std::string& error,
							 const std::string& path) {
	if (!config) return true;
	JPH::ObjectLayer layer = 0;
	if (!ResolveObjectLayer(config->layer_config, name, layer, error)) {
		error = path + ": " + error;
		return false;
	}
	return true;
}

bool ValidateSceneAssetData(const PhysicsSceneAsset& scene,
							const PhysicsConfig* config,
							std::string& error) {
	std::unordered_set<std::string> material_names;
	for (const auto& material : scene.materials) {
		if (material.name.empty()) {
			error = "material name must not be empty";
			return false;
		}
		if (!material_names.insert(material.name).second) {
			error = "duplicate material name: " + material.name;
			return false;
		}
		if (!std::isfinite(material.friction) || material.friction < 0.0f ||
			!std::isfinite(material.restitution) || material.restitution < 0.0f) {
			error = "invalid material properties: " + material.name;
			return false;
		}
	}

	std::unordered_set<std::string> scene_body_names;
	for (const auto& body : scene.static_bodies) {
		if (body.id.empty()) {
			error = "static body id must not be empty";
			return false;
		}
		if (IsReservedConstraintBodyName(body.id)) {
			error = "static body id uses reserved constraint name: " + body.id;
			return false;
		}
		if (!scene_body_names.insert(body.id).second) {
			error = "duplicate scene body id: " + body.id;
			return false;
		}
		if (!IsMaterialValid(body.material)) {
			error = "static body '" + body.id + "': invalid material";
			return false;
		}
		if (!IsFiniteDoubleVec(body.transform.position, 3) ||
			!IsFiniteFloatVec(body.transform.rotation, 4)) {
			error = "static body '" + body.id + "': invalid transform";
			return false;
		}
		if (!ValidateObjectLayerName(
				config, body.object_layer, error, "static body '" + body.id + "'")) {
			return false;
		}
		if (!ValidateShapeDefBasic(
				body.shape, material_names, error, "static body '" + body.id + "'")) {
			return false;
		}
	}

	std::unordered_set<std::string> prototype_ids;
	for (const auto& proto : scene.dynamic_prototypes) {
		if (proto.proto_id.empty()) {
			error = "prototype id must not be empty";
			return false;
		}
		if (!prototype_ids.insert(proto.proto_id).second) {
			error = "duplicate prototype id: " + proto.proto_id;
			return false;
		}
		if (!std::isfinite(proto.mass) || proto.mass <= 0.0f ||
			!IsMaterialValid(proto.material) || !IsMotionTypeName(proto.motion_type) ||
			!IsMotionQualityName(proto.motion_quality) || !IsFiniteFloat(proto.linear_damping) ||
			proto.linear_damping < 0.0f || !IsFiniteFloat(proto.angular_damping) ||
			proto.angular_damping < 0.0f || !IsFiniteFloat(proto.gravity_factor) ||
			!IsFiniteFloat(proto.max_linear_velocity) || proto.max_linear_velocity <= 0.0f ||
			!IsFiniteFloat(proto.max_angular_velocity) || proto.max_angular_velocity <= 0.0f) {
			error = "prototype '" + proto.proto_id + "': invalid physical properties";
			return false;
		}
		uint8_t dof_mask = 0;
		std::string dof_error;
		if (!BuildAllowedDofs(proto.allowed_dofs, dof_mask, dof_error)) {
			error = "prototype '" + proto.proto_id + "': " + dof_error;
			return false;
		}
		if (!ValidateObjectLayerName(
				config, proto.object_layer, error, "prototype '" + proto.proto_id + "'")) {
			return false;
		}
		if (!ValidateShapeDefBasic(
				proto.shape, material_names, error, "prototype '" + proto.proto_id + "'")) {
			return false;
		}
	}

	for (const auto& body : scene.dynamic_bodies) {
		if (body.id.empty()) {
			error = "dynamic body id must not be empty";
			return false;
		}
		if (IsReservedConstraintBodyName(body.id)) {
			error = "dynamic body id uses reserved constraint name: " + body.id;
			return false;
		}
		if (!scene_body_names.insert(body.id).second) {
			error = "duplicate scene body id: " + body.id;
			return false;
		}
		if (prototype_ids.find(body.proto_id) == prototype_ids.end()) {
			error = "dynamic body '" + body.id + "': prototype '" + body.proto_id + "' not found";
			return false;
		}
		if (!IsFiniteDoubleVec(body.transform.position, 3) ||
			!IsFiniteFloatVec(body.transform.rotation, 4)) {
			error = "dynamic body '" + body.id + "': invalid transform";
			return false;
		}
		if (body.linear_velocity.has_value() && !IsFiniteFloatVec(*body.linear_velocity, 3)) {
			error = "dynamic body '" + body.id + "': invalid linearVelocity";
			return false;
		}
		if (body.angular_velocity.has_value() && !IsFiniteFloatVec(*body.angular_velocity, 3)) {
			error = "dynamic body '" + body.id + "': invalid angularVelocity";
			return false;
		}
	}

	for (const auto& constraint : scene.constraints) {
		auto resolve_body = [&](const std::string& name) {
			return IsReservedConstraintBodyName(name) ||
				   scene_body_names.find(name) != scene_body_names.end();
		};
		if (constraint.type != "hinge" && constraint.type != "spring" &&
			constraint.type != "slider" && constraint.type != "fixed") {
			error = "unknown constraint type: " + constraint.type;
			return false;
		}
		if (!resolve_body(constraint.body_a) || !resolve_body(constraint.body_b)) {
			error = "constraint '" + constraint.body_a + "' -> '" + constraint.body_b +
					"': referenced body not found";
			return false;
		}
		if (IsReservedConstraintBodyName(constraint.body_a) &&
			IsReservedConstraintBodyName(constraint.body_b)) {
			error = "constraint '" + constraint.body_a + "' -> '" + constraint.body_b +
					"': at least one side must reference a body";
			return false;
		}
		if (constraint.body_a == constraint.body_b) {
			error = "constraint '" + constraint.body_a + "' -> '" + constraint.body_b +
					"': cannot constrain a body to itself";
			return false;
		}
		if (!IsFiniteDoubleVec(constraint.pivot, 3) ||
			!IsFiniteFloatRangeVec(constraint.axis, 3) ||
			(constraint.axis[0] * constraint.axis[0] + constraint.axis[1] * constraint.axis[1] +
			 constraint.axis[2] * constraint.axis[2]) <= 1.0e-12 ||
			!std::isfinite(constraint.limits.min) || !std::isfinite(constraint.limits.max) ||
			constraint.limits.min > constraint.limits.max ||
			!std::isfinite(constraint.spring.frequency) || constraint.spring.frequency < 0.0f ||
			!std::isfinite(constraint.spring.damping) || constraint.spring.damping < 0.0f) {
			error = "constraint '" + constraint.body_a + "' -> '" + constraint.body_b +
					"': invalid pivot/axis/limits/spring";
			return false;
		}
		if (constraint.axis2.has_value() &&
			(!IsFiniteFloatRangeVec(*constraint.axis2, 3) ||
			 ((*constraint.axis2)[0] * (*constraint.axis2)[0] +
			  (*constraint.axis2)[1] * (*constraint.axis2)[1] +
			  (*constraint.axis2)[2] * (*constraint.axis2)[2]) <= 1.0e-12)) {
			error = "constraint '" + constraint.body_a + "' -> '" + constraint.body_b +
					"': invalid axis2";
			return false;
		}
	}

	return true;
}

int LuaValidateSceneAssetJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 2);
	std::string validation_error = result.error;
	if (validation_error.empty()) {
		BindingContext ctx = GetContext(L);
		std::optional<PhysicsConfig> config =
			ctx.system ? ctx.system->GetPhysicsConfigSnapshot() : std::nullopt;
		const PhysicsConfig* config_ptr = config.has_value() ? &*config : nullptr;
		ValidateSceneAssetData(result.scene, config_ptr, validation_error);
	}
	lua_newtable(L);
	SetField(L, "valid", validation_error.empty());
	SetField(L, "error", validation_error);
	if (validation_error.empty()) {
		PushSceneCounts(L, result.scene);
		lua_setfield(L, -2, "counts");
	}
	return 1;
}

template <typename T, typename MatchFn, typename PushFn>
int PushSceneEntryByPredicate(lua_State* L,
							  const std::vector<T>& values,
							  MatchFn match,
							  PushFn push) {
	for (const auto& value : values) {
		if (match(value)) {
			push(L, value);
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

int LuaFindSceneMaterialJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 3);
	if (!result.error.empty()) return PushNilError(L, result.error.c_str());
	const char* name = luaL_checkstring(L, 2);
	return PushSceneEntryByPredicate(
		L,
		result.scene.materials,
		[&](const MaterialEntry& material) { return material.name == name; },
		PushMaterialEntry);
}

int LuaFindSceneStaticBodyJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 3);
	if (!result.error.empty()) return PushNilError(L, result.error.c_str());
	const char* id = luaL_checkstring(L, 2);
	return PushSceneEntryByPredicate(
		L,
		result.scene.static_bodies,
		[&](const JsonStaticBody& body) { return body.id == id; },
		PushStaticBody);
}

int LuaFindSceneDynamicPrototypeJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 3);
	if (!result.error.empty()) return PushNilError(L, result.error.c_str());
	const char* proto_id = luaL_checkstring(L, 2);
	return PushSceneEntryByPredicate(
		L,
		result.scene.dynamic_prototypes,
		[&](const JsonDynamicPrototype& proto) { return proto.proto_id == proto_id; },
		PushDynamicPrototype);
}

int LuaFindSceneDynamicBodyJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 3);
	if (!result.error.empty()) return PushNilError(L, result.error.c_str());
	const char* id = luaL_checkstring(L, 2);
	return PushSceneEntryByPredicate(
		L,
		result.scene.dynamic_bodies,
		[&](const JsonDynamicBody& body) { return body.id == id; },
		PushDynamicBody);
}

int LuaGetSceneConstraintJson(lua_State* L) {
	auto result = ReadSceneJson(L, 1, 3);
	if (!result.error.empty()) return PushNilError(L, result.error.c_str());
	lua_Integer index = luaL_checkinteger(L, 2);
	if (index < 1 ||
		static_cast<size_t>(index) > result.scene.constraints.size()) {
		lua_pushnil(L);
		return 1;
	}
	PushConstraint(L, result.scene.constraints[static_cast<size_t>(index - 1)]);
	return 1;
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
	SetField(L, "empty", table.Empty());
	lua_newtable(L);
	auto names = table.GetNames();
	for (size_t i = 0; i < names.size(); ++i) {
		lua_pushlstring(L, names[i].data(), names[i].size());
		lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
	}
	lua_setfield(L, -2, "names");
	auto entries = table.GetEntries();
	PushArray(L, entries, PushMaterialEntry);
	lua_setfield(L, -2, "materials");
	PushArray(L, entries, PushMaterialEntry);
	lua_setfield(L, -2, "entries");
	lua_newtable(L);
	for (const auto& entry : entries) {
		PushMaterialEntry(L, entry);
		lua_setfield(L, -2, entry.name.c_str());
	}
	lua_setfield(L, -2, "by_name");
	lua_getfield(L, -1, "by_name");
	lua_setfield(L, -2, "byName");
	return 1;
}

int CheckNonNegativeInt(lua_State* L, int index, const char* name) {
	lua_Integer value = luaL_checkinteger(L, index);
	luaL_argcheck(L,
				  value >= 0 &&
					  value <= static_cast<lua_Integer>((std::numeric_limits<int>::max)()),
				  index,
				  name);
	return static_cast<int>(value);
}

int OptionalNonNegativeInt(lua_State* L, int index, int fallback, const char* name) {
	return lua_gettop(L) >= index && !lua_isnil(L, index)
			   ? CheckNonNegativeInt(L, index, name)
			   : fallback;
}

int LuaMakeAssetLoadResult(lua_State* L) {
	AssetLoadResult result;
	result.success = lua_gettop(L) >= 1 ? lua_toboolean(L, 1) != 0 : false;
	if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
		result.error = luaL_checkstring(L, 2);
	}
	result.static_bodies_loaded =
		OptionalNonNegativeInt(L, 3, 0, "static_bodies_loaded must be >= 0");
	result.dynamic_prototypes_loaded =
		OptionalNonNegativeInt(L, 4, 0, "dynamic_prototypes_loaded must be >= 0");
	result.dynamic_bodies_loaded =
		OptionalNonNegativeInt(L, 5, 0, "dynamic_bodies_loaded must be >= 0");
	result.constraints_loaded =
		OptionalNonNegativeInt(L, 6, 0, "constraints_loaded must be >= 0");
	result.scene_objects_loaded =
		OptionalNonNegativeInt(L, 7, 0, "scene_objects_loaded must be >= 0");
	PushAssetLoadResult(L, result);
	return 1;
}

int LuaMakeSceneBodyRecord(lua_State* L) {
	PhysicsSceneBodyRecord record;
	record.body_id = CheckUInt32(L, 1, "body_id must be a uint32");
	record.asset_id = luaL_checkstring(L, 2);

	std::vector<double> position;
	std::vector<float> rotation;
	std::vector<float> linear_velocity;
	std::vector<float> angular_velocity;
	std::string error;
	if (!ReadNumberArray(L, 3, 3, position, error) || !IsFiniteDoubleVec(position, 3)) {
		return PushNilError(L, error.empty() ? "invalid position" : error.c_str());
	}
	if (!ReadFloatArray(L, 4, 4, rotation, error) || !IsFiniteFloatVec(rotation, 4)) {
		return PushNilError(L, error.empty() ? "invalid rotation" : error.c_str());
	}
	if (lua_gettop(L) >= 5 && !lua_isnil(L, 5)) {
		if (!ReadFloatArray(L, 5, 3, linear_velocity, error) ||
			!IsFiniteFloatVec(linear_velocity, 3)) {
			return PushNilError(L, error.empty() ? "invalid linear velocity" : error.c_str());
		}
	} else {
		linear_velocity = {0.0f, 0.0f, 0.0f};
	}
	if (lua_gettop(L) >= 6 && !lua_isnil(L, 6)) {
		if (!ReadFloatArray(L, 6, 3, angular_velocity, error) ||
			!IsFiniteFloatVec(angular_velocity, 3)) {
			return PushNilError(L, error.empty() ? "invalid angular velocity" : error.c_str());
		}
	} else {
		angular_velocity = {0.0f, 0.0f, 0.0f};
	}

	record.position = ParsePhysicsVec3(position);
	record.rotation = ParsePhysicsQuat(rotation);
	record.linear_velocity = ParsePhysicsFloatVec3(linear_velocity);
	record.angular_velocity = ParsePhysicsFloatVec3(angular_velocity);
	record.dynamic = lua_gettop(L) >= 7 && lua_toboolean(L, 7) != 0;
	PushSceneBodyRecord(L, record);
	return 1;
}

int LuaHasMaterialInJson(lua_State* L) {
	size_t len = 0;
	const char* json = luaL_checklstring(L, 1, &len);
	const char* name = luaL_checkstring(L, 2);
	MaterialTable table;
	if (!table.LoadFromJson(std::string(json, len))) {
		return PushNilError(L, "materials json parse failed");
	}
	lua_pushboolean(L, table.Has(name) ? 1 : 0);
	return 1;
}

int LuaGetMaterialInJson(lua_State* L) {
	size_t len = 0;
	const char* json = luaL_checklstring(L, 1, &len);
	const char* name = luaL_checkstring(L, 2);
	MaterialTable table;
	if (!table.LoadFromJson(std::string(json, len))) {
		return PushNilError(L, "materials json parse failed");
	}
	for (const auto& entry : table.GetEntries()) {
		if (entry.name == name) {
			PushMaterialEntry(L, entry);
			return 1;
		}
	}
	lua_pushnil(L);
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

void PushRVec3(lua_State* L, JPH::RVec3Arg value) {
	lua_newtable(L);
	SetField(L, "x", value.GetX());
	SetField(L, "y", value.GetY());
	SetField(L, "z", value.GetZ());
	lua_pushnumber(L, value.GetX());
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, value.GetY());
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, value.GetZ());
	lua_rawseti(L, -2, 3);
}

void PushVec3(lua_State* L, JPH::Vec3Arg value) {
	lua_newtable(L);
	SetField(L, "x", value.GetX());
	SetField(L, "y", value.GetY());
	SetField(L, "z", value.GetZ());
	lua_pushnumber(L, value.GetX());
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, value.GetY());
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, value.GetZ());
	lua_rawseti(L, -2, 3);
}

void PushQuat(lua_State* L, JPH::QuatArg value) {
	lua_newtable(L);
	SetField(L, "x", value.GetX());
	SetField(L, "y", value.GetY());
	SetField(L, "z", value.GetZ());
	SetField(L, "w", value.GetW());
	lua_pushnumber(L, value.GetX());
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, value.GetY());
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, value.GetZ());
	lua_rawseti(L, -2, 3);
	lua_pushnumber(L, value.GetW());
	lua_rawseti(L, -2, 4);
}

int LuaParseVec3(lua_State* L) {
	std::vector<double> values;
	std::string error;
	if (!ReadNumberArray(L, 1, 3, values, error) || !IsFiniteDoubleVec(values, 3)) {
		return PushNilError(L, error.empty() ? "invalid vec3" : error.c_str());
	}
	PushRVec3(L, ParsePhysicsVec3(values));
	return 1;
}

int LuaParseFloatVec3(lua_State* L) {
	std::vector<float> values;
	std::string error;
	if (!ReadFloatArray(L, 1, 3, values, error) || !IsFiniteFloatVec(values, 3)) {
		return PushNilError(L, error.empty() ? "invalid float vec3" : error.c_str());
	}
	PushVec3(L, ParsePhysicsFloatVec3(values));
	return 1;
}

int LuaParseQuat(lua_State* L) {
	std::vector<float> values;
	std::string error;
	if (!ReadFloatArray(L, 1, 4, values, error) || !IsFiniteFloatVec(values, 4)) {
		return PushNilError(L, error.empty() ? "invalid quaternion" : error.c_str());
	}
	PushQuat(L, ParsePhysicsQuat(values));
	return 1;
}

int LuaNormalizeQuat(lua_State* L) {
	std::vector<float> values;
	std::string error;
	if (!ReadFloatArray(L, 1, 4, values, error) || !IsFiniteFloatVec(values, 4)) {
		return PushNilError(L, error.empty() ? "invalid quaternion" : error.c_str());
	}
	PushQuat(L, NormalizePhysicsQuat(JPH::Quat(values[0], values[1], values[2], values[3])));
	return 1;
}

int LuaIsPositiveFinite(lua_State* L) {
	double value = luaL_checknumber(L, 1);
	lua_pushboolean(L, IsPositiveFinite(value) ? 1 : 0);
	return 1;
}

int LuaIsFiniteFloat(lua_State* L) {
	double value = luaL_checknumber(L, 1);
	bool finite = value >= -static_cast<double>((std::numeric_limits<float>::max)()) &&
				  value <= static_cast<double>((std::numeric_limits<float>::max)()) &&
				  IsFiniteFloat(static_cast<float>(value));
	lua_pushboolean(L, finite ? 1 : 0);
	return 1;
}

int LuaIsFiniteDoubleVec(lua_State* L) {
	lua_Integer expected_arg = luaL_checkinteger(L, 2);
	luaL_argcheck(L, expected_arg >= 0, 2, "expected length must be >= 0");
	size_t expected = static_cast<size_t>(expected_arg);
	std::vector<double> values;
	std::string error;
	bool ok = ReadNumberArray(L, 1, expected, values, error) && IsFiniteDoubleVec(values, expected);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaIsFiniteFloatVec(lua_State* L) {
	lua_Integer expected_arg = luaL_checkinteger(L, 2);
	luaL_argcheck(L, expected_arg >= 0, 2, "expected length must be >= 0");
	size_t expected = static_cast<size_t>(expected_arg);
	std::vector<float> values;
	std::string error;
	bool ok = ReadFloatArray(L, 1, expected, values, error) && IsFiniteFloatVec(values, expected);
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

int LuaIsMaterialValid(lua_State* L) {
	JsonMaterial material;
	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "friction");
		double friction = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : -1.0;
		lua_pop(L, 1);
		lua_getfield(L, 1, "restitution");
		double restitution = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : -1.0;
		lua_pop(L, 1);
		if (!std::isfinite(friction) ||
			friction < -static_cast<double>((std::numeric_limits<float>::max)()) ||
			friction > static_cast<double>((std::numeric_limits<float>::max)()) ||
			!std::isfinite(restitution) ||
			restitution < -static_cast<double>((std::numeric_limits<float>::max)()) ||
			restitution > static_cast<double>((std::numeric_limits<float>::max)())) {
			lua_pushboolean(L, 0);
			return 1;
		}
		material.friction = static_cast<float>(friction);
		material.restitution = static_cast<float>(restitution);
	} else {
		material.friction = CheckFiniteFloat(L, 1, "friction must be finite");
		material.restitution = CheckFiniteFloat(L, 2, "restitution must be finite");
	}
	lua_pushboolean(L, IsMaterialValid(material) ? 1 : 0);
	return 1;
}

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

int LuaParseMotionType(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	auto type = ParseMotionType(name);
	lua_newtable(L);
	SetField(L, "name", MotionTypeName(type));
	SetField(L, "value", static_cast<int>(type));
	return 1;
}

int LuaParseMotionQuality(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	auto quality = ParseMotionQuality(name);
	lua_newtable(L);
	SetField(L, "name", MotionQualityName(quality));
	SetField(L, "value", static_cast<int>(quality));
	return 1;
}

std::optional<JPH::ObjectLayer> ResolveObjectLayerByName(const PhysicsConfig& config,
														 const std::string& name,
														 std::string& error) {
	JPH::ObjectLayer layer = 0;
	if (!ResolveObjectLayer(config.layer_config, name, layer, error)) {
		return std::nullopt;
	}
	return layer;
}

std::optional<JPH::BroadPhaseLayer> ResolveBroadPhaseLayerByName(const PhysicsConfig& config,
																 const std::string& name,
																 std::string& error) {
	auto it = config.layer_config.broad_phase_layers.find(name);
	if (it == config.layer_config.broad_phase_layers.end()) {
		error = "unknown broadPhaseLayer '" + name + "'";
		return std::nullopt;
	}
	return JPH::BroadPhaseLayer(it->second);
}

int LuaGetBroadPhaseLayer(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const char* name = luaL_checkstring(L, 1);
	auto config = ctx.system->GetPhysicsConfigSnapshot();
	if (!config.has_value()) {
		return PushNilError(L, "physics config not available");
	}
	std::string error;
	auto object_layer = ResolveObjectLayerByName(*config, name, error);
	if (!object_layer.has_value()) {
		return PushNilError(L, error.c_str());
	}
	BPLayerInterfaceImpl bp_iface(config->layer_config);
	auto bp_layer = bp_iface.GetBroadPhaseLayer(*object_layer);
	lua_pushinteger(L, static_cast<lua_Integer>(bp_layer.GetValue()));
	return 1;
}

int LuaObjectLayersShouldCollide(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const char* lhs = luaL_checkstring(L, 1);
	const char* rhs = luaL_checkstring(L, 2);
	auto config = ctx.system->GetPhysicsConfigSnapshot();
	if (!config.has_value()) {
		return PushNilError(L, "physics config not available");
	}
	std::string error;
	auto lhs_layer = ResolveObjectLayerByName(*config, lhs, error);
	if (!lhs_layer.has_value()) return PushNilError(L, error.c_str());
	auto rhs_layer = ResolveObjectLayerByName(*config, rhs, error);
	if (!rhs_layer.has_value()) return PushNilError(L, error.c_str());
	ObjectLayerPairFilterImpl filter(config->layer_config);
	lua_pushboolean(L, filter.ShouldCollide(*lhs_layer, *rhs_layer) ? 1 : 0);
	return 1;
}

int LuaObjectVsBroadPhaseShouldCollide(lua_State* L) {
	BindingContext ctx;
	if (!CheckSystem(L, ctx)) return 2;
	const char* object_name = luaL_checkstring(L, 1);
	auto config = ctx.system->GetPhysicsConfigSnapshot();
	if (!config.has_value()) {
		return PushNilError(L, "physics config not available");
	}
	std::string error;
	auto object_layer = ResolveObjectLayerByName(*config, object_name, error);
	if (!object_layer.has_value()) return PushNilError(L, error.c_str());

	std::optional<JPH::BroadPhaseLayer> bp_layer;
	if (lua_isinteger(L, 2)) {
		lua_Integer value = lua_tointeger(L, 2);
		if (value < 0 || value > 255) {
			return PushNilError(L, "broad phase layer value out of range");
		}
		bp_layer = JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(value));
	} else {
		const char* bp_name = luaL_checkstring(L, 2);
		bp_layer = ResolveBroadPhaseLayerByName(*config, bp_name, error);
	}
	if (!bp_layer.has_value()) return PushNilError(L, error.c_str());

	BPLayerInterfaceImpl bp_iface(config->layer_config);
	ObjectVSBLayerFilterImpl filter(config->layer_config, bp_iface);
	lua_pushboolean(L, filter.ShouldCollide(*object_layer, *bp_layer) ? 1 : 0);
	return 1;
}

const luaL_Reg kAssetFunctions[] = {{"load_configured_scene_asset", LuaLoadConfiguredSceneAsset},
									{"load_scene_asset", LuaLoadSceneAsset},
									{"parse_scene_asset_json", LuaParseSceneAssetJson},
									{"validate_scene_asset_json", LuaValidateSceneAssetJson},
									{"find_scene_material_json", LuaFindSceneMaterialJson},
									{"find_scene_static_body_json", LuaFindSceneStaticBodyJson},
									{"find_scene_dynamic_prototype_json",
									 LuaFindSceneDynamicPrototypeJson},
									{"find_scene_dynamic_body_json",
									 LuaFindSceneDynamicBodyJson},
									{"get_scene_constraint_json", LuaGetSceneConstraintJson},
									{"parse_materials_json", LuaParseMaterialsJson},
									{"make_asset_load_result", LuaMakeAssetLoadResult},
									{"make_scene_body_record", LuaMakeSceneBodyRecord},
									{"has_material_in_json", LuaHasMaterialInJson},
									{"get_material_in_json", LuaGetMaterialInJson},
									{"parse_vec3", LuaParseVec3},
									{"parse_float_vec3", LuaParseFloatVec3},
									{"parse_quat", LuaParseQuat},
									{"normalize_quat", LuaNormalizeQuat},
									{"is_positive_finite", LuaIsPositiveFinite},
									{"is_finite_float", LuaIsFiniteFloat},
									{"is_finite_double_vec", LuaIsFiniteDoubleVec},
									{"is_finite_float_vec", LuaIsFiniteFloatVec},
									{"is_material_valid", LuaIsMaterialValid},
									{"parse_motion_type", LuaParseMotionType},
									{"parse_motion_quality", LuaParseMotionQuality},
									{"is_motion_type_name", LuaIsMotionTypeName},
									{"is_motion_quality_name", LuaIsMotionQualityName},
									{"build_allowed_dofs", LuaBuildAllowedDofs},
									{"resolve_object_layer", LuaResolveObjectLayer},
									{"get_broad_phase_layer", LuaGetBroadPhaseLayer},
									{"object_layers_should_collide", LuaObjectLayersShouldCollide},
									{"object_vs_broad_phase_should_collide",
									 LuaObjectVsBroadPhaseShouldCollide},
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
