#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_assets.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <utility>

#include <glaze/json.hpp>

namespace engine {

struct JsonAssetFile {
	std::optional<std::vector<JsonStaticBody>> static_bodies;
	std::optional<std::vector<JsonDynamicPrototype>> dynamic_prototypes;
	std::optional<std::vector<JsonDynamicBody>> dynamic_bodies;
	std::optional<std::vector<JsonConstraint>> constraints;
	std::optional<std::vector<MaterialEntry>> materials;
};

namespace {

bool RegisterSceneBodyName(const std::string& name,
						   JPH::BodyID body_id,
						   std::unordered_set<std::string>& names,
						   PhysicsConstraintAssetFactory::BodyLookup& lookup,
						   std::string& out_error) {
	if (name.empty()) {
		out_error = "scene body id must not be empty";
		return false;
	}
	if (name == "world" || name == "fixed" || name == "__world__") {
		out_error = "scene body id uses reserved constraint body name: " + name;
		return false;
	}
	if (!names.insert(name).second) {
		out_error = "duplicate scene body id: " + name;
		return false;
	}
	lookup[name] = body_id;
	return true;
}

PhysicsSceneBodyRecord MakeBodyRecord(const BodySettingsResult& settings_result, JPH::BodyID id) {
	PhysicsSceneBodyRecord record;
	record.body_id = id.GetIndexAndSequenceNumber();
	record.asset_id = settings_result.id;
	record.position = settings_result.settings.mPosition;
	record.rotation = settings_result.settings.mRotation;
	record.linear_velocity = settings_result.settings.mLinearVelocity;
	record.angular_velocity = settings_result.settings.mAngularVelocity;
	record.dynamic = settings_result.dynamic;
	return record;
}

bool ValidateSceneMaterials(const std::vector<MaterialEntry>& materials, std::string& out_error) {
	std::unordered_set<std::string> names;
	for (const auto& material : materials) {
		if (material.name.empty()) {
			out_error = "material name must not be empty";
			return false;
		}
		if (!names.insert(material.name).second) {
			out_error = "duplicate material name: " + material.name;
			return false;
		}
		if (!std::isfinite(material.friction) || material.friction < 0.0f ||
			!std::isfinite(material.restitution) || material.restitution < 0.0f) {
			out_error = "invalid material properties: " + material.name;
			return false;
		}
	}
	return true;
}

}  // namespace

}  // namespace engine

template <>
struct glz::meta<engine::JsonMaterial> {
	using T = engine::JsonMaterial;
	static constexpr auto value =
		glz::object("friction", &T::friction, "restitution", &T::restitution);
};

template <>
struct glz::meta<engine::JsonTransform> {
	using T = engine::JsonTransform;
	static constexpr auto value = glz::object("position", &T::position, "rotation", &T::rotation);
};

template <>
struct glz::meta<engine::JsonShapeDef> {
	using T = engine::JsonShapeDef;
	static constexpr auto value = glz::object("type",
											  &T::type,
											  "params",
											  &T::params,
											  "shapes",
											  &T::shapes,
											  "position",
											  &T::position,
											  "rotation",
											  &T::rotation,
											  "material",
											  &T::material);
};

template <>
struct glz::meta<engine::JsonStaticBody> {
	using T = engine::JsonStaticBody;
	static constexpr auto value = glz::object("id",
											  &T::id,
											  "shape",
											  &T::shape,
											  "material",
											  &T::material,
											  "transform",
											  &T::transform,
											  "objectLayer",
											  &T::object_layer);
};

template <>
struct glz::meta<engine::JsonDynamicPrototype> {
	using T = engine::JsonDynamicPrototype;
	static constexpr auto value = glz::object("protoId",
											  &T::proto_id,
											  "shape",
											  &T::shape,
											  "mass",
											  &T::mass,
											  "material",
											  &T::material,
											  "motionType",
											  &T::motion_type,
											  "motionQuality",
											  &T::motion_quality,
											  "linearDamping",
											  &T::linear_damping,
											  "angularDamping",
											  &T::angular_damping,
											  "gravityFactor",
											  &T::gravity_factor,
											  "objectLayer",
											  &T::object_layer,
											  "allowedDofs",
											  &T::allowed_dofs,
											  "isSensor",
											  &T::is_sensor,
											  "allowSleeping",
											  &T::allow_sleeping,
											  "maxLinearVelocity",
											  &T::max_linear_velocity,
											  "maxAngularVelocity",
											  &T::max_angular_velocity);
};

template <>
struct glz::meta<engine::JsonDynamicBody> {
	using T = engine::JsonDynamicBody;
	static constexpr auto value = glz::object("id",
											  &T::id,
											  "protoId",
											  &T::proto_id,
											  "transform",
											  &T::transform,
											  "userData",
											  &T::user_data,
											  "activate",
											  &T::activate,
											  "linearVelocity",
											  &T::linear_velocity,
											  "angularVelocity",
											  &T::angular_velocity);
};

template <>
struct glz::meta<engine::JsonConstraint::ConstraintLimits> {
	using T = engine::JsonConstraint::ConstraintLimits;
	static constexpr auto value = glz::object("min", &T::min, "max", &T::max);
};

template <>
struct glz::meta<engine::JsonConstraint::ConstraintSpring> {
	using T = engine::JsonConstraint::ConstraintSpring;
	static constexpr auto value =
		glz::object("frequency", &T::frequency, "damping", &T::damping);
};

template <>
struct glz::meta<engine::JsonConstraint> {
	using T = engine::JsonConstraint;
	static constexpr auto value = glz::object("type",
											  &T::type,
											  "bodyA",
											  &T::body_a,
											  "bodyB",
											  &T::body_b,
											  "pivot",
											  &T::pivot,
											  "axis",
											  &T::axis,
											  "axis2",
											  &T::axis2,
											  "limits",
											  &T::limits,
											  "spring",
											  &T::spring);
};

template <>
struct glz::meta<engine::JsonAssetFile> {
	using T = engine::JsonAssetFile;
	static constexpr auto value = glz::object("staticBodies",
											  &T::static_bodies,
											  "dynamicPrototypes",
											  &T::dynamic_prototypes,
											  "dynamicBodies",
											  &T::dynamic_bodies,
											  "constraints",
											  &T::constraints,
											  "materials",
											  &T::materials);
};

namespace engine {

PhysicsSceneAssetLoadResult PhysicsSceneAsset::LoadFromFile(const std::string& json_path) {
	PhysicsSceneAssetLoadResult result;
	std::ifstream f(json_path, std::ios::binary);
	if (!f) {
		result.error = "asset file not found: " + json_path;
		return result;
	}
	std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return LoadFromJson(json, json_path);
}

PhysicsSceneAssetLoadResult PhysicsSceneAsset::LoadFromJson(const std::string& json,
															const std::string& source_path) {
	PhysicsSceneAssetLoadResult result;
	JsonAssetFile asset;
	auto ec = glz::read_json(asset, json);
	if (ec) {
		result.error = "parse error in " + source_path + ": " + glz::format_error(ec, json);
		return result;
	}

	result.scene.source_path = source_path;
	result.scene.assets_dir = GetAssetDirectory(source_path);
	if (asset.materials) result.scene.materials = std::move(*asset.materials);
	if (asset.static_bodies) result.scene.static_bodies = std::move(*asset.static_bodies);
	if (asset.dynamic_prototypes) {
		result.scene.dynamic_prototypes = std::move(*asset.dynamic_prototypes);
	}
	if (asset.dynamic_bodies) result.scene.dynamic_bodies = std::move(*asset.dynamic_bodies);
	if (asset.constraints) result.scene.constraints = std::move(*asset.constraints);
	return result;
}

JPH::RVec3 AssetLoader::ParseVec3(const std::vector<double>& v) {
	return ParsePhysicsVec3(v);
}

JPH::Quat AssetLoader::ParseQuat(const std::vector<float>& q) {
	return ParsePhysicsQuat(q);
}

AssetLoader::ShapeCreateResult AssetLoader::CreateShape(const JsonShapeDef& def,
														const MaterialTable& material_table,
														const std::string& assets_dir) {
	return PhysicsShapeAssetFactory::CreateShape(def, material_table, assets_dir);
}

AssetLoadResult AssetLoader::LoadScene(const std::string& json_path,
									   JPH::BodyInterface& body_interface,
									   JPH::PhysicsSystem& physics_system,
									   const MaterialTable& material_table,
									   const LayerConfig& layer_config) {
	AssetLoadResult result;
	prototypes_.clear();
	static_body_ids_.clear();
	scene_body_records_.clear();

	auto scene_result = PhysicsSceneAsset::LoadFromFile(json_path);
	if (!scene_result.error.empty()) {
		result.error = scene_result.error;
		return result;
	}
	const PhysicsSceneAsset& scene = scene_result.scene;

	MaterialTable combined_materials = material_table;
	if (!scene.materials.empty()) {
		if (!ValidateSceneMaterials(scene.materials, result.error)) {
			return result;
		}
		combined_materials.Register(scene.materials);
	}

	std::vector<JPH::BodyID> created_body_ids;
	std::vector<JPH::TwoBodyConstraint*> created_constraints;
	PhysicsConstraintAssetFactory::BodyLookup body_lookup;
	std::unordered_set<std::string> scene_body_names;
	std::unordered_set<std::string> proto_ids;

	auto cleanup = [&]() {
		for (JPH::TwoBodyConstraint* constraint : created_constraints) {
			physics_system.RemoveConstraint(constraint);
		}
		created_constraints.clear();
		for (JPH::BodyID id : created_body_ids) {
			if (body_interface.IsAdded(id)) {
				body_interface.RemoveBody(id);
			}
			body_interface.DestroyBody(id);
		}
		created_body_ids.clear();
		prototypes_.clear();
		static_body_ids_.clear();
		scene_body_records_.clear();
	};

	std::vector<JPH::BodyID> static_body_ids;
	static_body_ids.reserve(scene.static_bodies.size());
	for (const auto& static_body : scene.static_bodies) {
		auto body_result = PhysicsBodyAssetFactory::BuildStaticBody(
			static_body, combined_materials, layer_config, scene.assets_dir);
		if (!body_result.error.empty()) {
			result.error = body_result.error;
			cleanup();
			return result;
		}

		JPH::Body* body = body_interface.CreateBody(body_result.settings);
		if (!body) {
			result.error = "static body '" + static_body.id + "': creation failed";
			cleanup();
			return result;
		}

		JPH::BodyID bid = body->GetID();
		std::string register_error;
		if (!RegisterSceneBodyName(static_body.id,
								   bid,
								   scene_body_names,
								   body_lookup,
								   register_error)) {
			result.error = register_error;
			body_interface.DestroyBody(bid);
			cleanup();
			return result;
		}

		created_body_ids.push_back(bid);
		static_body_ids.push_back(bid);
		uint32_t body_id = bid.GetIndexAndSequenceNumber();
		static_body_ids_[body_id] = static_body.id;
		scene_body_records_.push_back(MakeBodyRecord(body_result, bid));
		++result.static_bodies_loaded;
		++result.scene_objects_loaded;
	}

	if (!static_body_ids.empty()) {
		JPH::BodyInterface::AddState add_state = body_interface.AddBodiesPrepare(
			static_body_ids.data(), static_cast<int>(static_body_ids.size()));
		body_interface.AddBodiesFinalize(static_body_ids.data(),
										 static_cast<int>(static_body_ids.size()),
										 add_state,
										 JPH::EActivation::DontActivate);
	}

	for (const auto& proto : scene.dynamic_prototypes) {
		if (!proto_ids.insert(proto.proto_id).second) {
			result.error = "duplicate prototype id: " + proto.proto_id;
			cleanup();
			return result;
		}

		auto proto_result = PhysicsBodyAssetFactory::BuildPrototype(
			proto, combined_materials, layer_config, scene.assets_dir);
		if (!proto_result.error.empty()) {
			result.error = proto_result.error;
			cleanup();
			return result;
		}

		prototypes_[proto_result.prototype.proto_id] = proto_result.prototype;
		++result.dynamic_prototypes_loaded;
	}

	for (const auto& dynamic_body : scene.dynamic_bodies) {
		auto body_result = PhysicsBodyAssetFactory::BuildDynamicBody(dynamic_body, prototypes_);
		if (!body_result.error.empty()) {
			result.error = body_result.error;
			cleanup();
			return result;
		}

		JPH::Body* body = body_interface.CreateBody(body_result.settings);
		if (!body) {
			result.error = "dynamic body '" + dynamic_body.id + "': creation failed";
			cleanup();
			return result;
		}

		JPH::BodyID bid = body->GetID();
		std::string register_error;
		if (!RegisterSceneBodyName(dynamic_body.id,
								   bid,
								   scene_body_names,
								   body_lookup,
								   register_error)) {
			result.error = register_error;
			body_interface.DestroyBody(bid);
			cleanup();
			return result;
		}

		created_body_ids.push_back(bid);
		body_interface.AddBody(bid, body_result.activation);
		scene_body_records_.push_back(MakeBodyRecord(body_result, bid));
		++result.dynamic_bodies_loaded;
		++result.scene_objects_loaded;
	}

	for (const auto& constraint : scene.constraints) {
		auto constraint_result = PhysicsConstraintAssetFactory::CreateAndAddConstraint(
			constraint, body_lookup, body_interface, physics_system);
		if (!constraint_result.success) {
			result.error = constraint_result.error;
			cleanup();
			return result;
		}
		created_constraints.push_back(constraint_result.constraint);
		++result.constraints_loaded;
	}

	result.success = true;
	return result;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
