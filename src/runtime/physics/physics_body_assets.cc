#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_body_assets.h"

#include <cmath>

namespace engine {

namespace {

bool IsBodyPhysicalPropertiesValid(float mass,
								   const JsonMaterial& material,
								   const std::string& motion_type,
								   const std::string& motion_quality,
								   float linear_damping,
								   float angular_damping,
								   float gravity_factor,
								   float max_linear_velocity,
								   float max_angular_velocity,
								   std::string& out_error) {
	if (!std::isfinite(mass) || mass <= 0.0f) {
		out_error = "mass must be finite and > 0";
		return false;
	}
	if (!IsMaterialValid(material) || !IsMotionTypeName(motion_type) ||
		!IsMotionQualityName(motion_quality) || !IsFiniteFloat(linear_damping) ||
		linear_damping < 0.0f || !IsFiniteFloat(angular_damping) || angular_damping < 0.0f ||
		!IsFiniteFloat(gravity_factor) || !IsFiniteFloat(max_linear_velocity) ||
		max_linear_velocity <= 0.0f || !IsFiniteFloat(max_angular_velocity) ||
		max_angular_velocity <= 0.0f) {
		out_error = "invalid physical properties";
		return false;
	}
	return true;
}

}  // namespace

PrototypeBuildResult PhysicsBodyAssetFactory::BuildPrototype(const JsonDynamicPrototype& proto,
															 const MaterialTable& material_table,
															 const LayerConfig& layer_config,
															 const std::string& assets_dir) {
	PrototypeBuildResult result;

	if (proto.proto_id.empty()) {
		result.error = "prototype id must not be empty";
		return result;
	}

	std::string validation_error;
	if (!IsBodyPhysicalPropertiesValid(proto.mass,
									   proto.material,
									   proto.motion_type,
									   proto.motion_quality,
									   proto.linear_damping,
									   proto.angular_damping,
									   proto.gravity_factor,
									   proto.max_linear_velocity,
									   proto.max_angular_velocity,
									   validation_error)) {
		result.error = "prototype '" + proto.proto_id + "': " + validation_error;
		return result;
	}

	auto shape_result = PhysicsShapeAssetFactory::CreateShape(proto.shape, material_table, assets_dir);
	if (!shape_result.shape) {
		result.error = "prototype '" + proto.proto_id + "': " + shape_result.error;
		return result;
	}

	JPH::ObjectLayer obj_layer = 0;
	if (!ResolveObjectLayer(layer_config, proto.object_layer, obj_layer, validation_error)) {
		result.error = "prototype '" + proto.proto_id + "': " + validation_error;
		return result;
	}

	uint8_t allowed_dofs = 0;
	if (!BuildAllowedDofs(proto.allowed_dofs, allowed_dofs, validation_error)) {
		result.error = "prototype '" + proto.proto_id + "': " + validation_error;
		return result;
	}

	PrototypeEntry entry;
	entry.proto_id = proto.proto_id;
	entry.shape = shape_result.shape;
	entry.mass = proto.mass;
	entry.friction = proto.material.friction;
	entry.restitution = proto.material.restitution;
	entry.motion_type = ParseMotionType(proto.motion_type);
	entry.motion_quality = ParseMotionQuality(proto.motion_quality);
	entry.linear_damping = proto.linear_damping;
	entry.angular_damping = proto.angular_damping;
	entry.gravity_factor = proto.gravity_factor;
	entry.object_layer = obj_layer;
	entry.allowed_dofs = allowed_dofs;
	entry.is_sensor = proto.is_sensor;
	entry.allow_sleeping = proto.allow_sleeping;
	entry.max_linear_velocity = proto.max_linear_velocity;
	entry.max_angular_velocity = proto.max_angular_velocity;

	result.prototype = entry;
	return result;
}

BodySettingsResult PhysicsBodyAssetFactory::BuildStaticBody(const JsonStaticBody& body,
															const MaterialTable& material_table,
															const LayerConfig& layer_config,
															const std::string& assets_dir) {
	BodySettingsResult result;
	result.id = body.id;

	if (body.id.empty()) {
		result.error = "static body id must not be empty";
		return result;
	}
	if (!IsMaterialValid(body.material)) {
		result.error = "static body '" + body.id + "': invalid material";
		return result;
	}
	if (!IsFiniteDoubleVec(body.transform.position, 3) ||
		!IsFiniteFloatVec(body.transform.rotation, 4)) {
		result.error = "static body '" + body.id + "': invalid transform";
		return result;
	}

	std::string layer_error;
	JPH::ObjectLayer obj_layer = 0;
	if (!ResolveObjectLayer(layer_config, body.object_layer, obj_layer, layer_error)) {
		result.error = "static body '" + body.id + "': " + layer_error;
		return result;
	}

	auto shape_result = PhysicsShapeAssetFactory::CreateShape(body.shape, material_table, assets_dir);
	if (!shape_result.shape) {
		result.error = "static body '" + body.id + "': " + shape_result.error;
		return result;
	}

	JPH::RVec3 pos = ParsePhysicsVec3(body.transform.position);
	JPH::Quat rot = ParsePhysicsQuat(body.transform.rotation);

	result.settings =
		JPH::BodyCreationSettings(shape_result.shape, pos, rot, JPH::EMotionType::Static, obj_layer);
	result.settings.mFriction = body.material.friction;
	result.settings.mRestitution = body.material.restitution;
	result.activation = JPH::EActivation::DontActivate;
	result.dynamic = false;
	return result;
}

BodySettingsResult PhysicsBodyAssetFactory::BuildDynamicBody(
	const JsonDynamicBody& body,
	const std::unordered_map<std::string, PrototypeEntry>& prototypes) {
	BodySettingsResult result;
	result.id = body.id;
	result.source_proto_id = body.proto_id;
	result.activation = body.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;

	if (body.id.empty()) {
		result.error = "dynamic body id must not be empty";
		return result;
	}
	if (body.proto_id.empty()) {
		result.error = "dynamic body '" + body.id + "': protoId must not be empty";
		return result;
	}
	if (!IsFiniteDoubleVec(body.transform.position, 3) ||
		!IsFiniteFloatVec(body.transform.rotation, 4)) {
		result.error = "dynamic body '" + body.id + "': invalid transform";
		return result;
	}

	auto proto_it = prototypes.find(body.proto_id);
	if (proto_it == prototypes.end()) {
		result.error = "dynamic body '" + body.id + "': prototype '" + body.proto_id + "' not found";
		return result;
	}

	JPH::RVec3 pos = ParsePhysicsVec3(body.transform.position);
	JPH::Quat rot = ParsePhysicsQuat(body.transform.rotation);
	result.settings = CreateBodySettingsFromPrototype(proto_it->second, pos, rot, body.user_data);
	result.dynamic = result.settings.mMotionType != JPH::EMotionType::Static;

	if (body.linear_velocity) {
		if (!IsFiniteFloatVec(*body.linear_velocity, 3)) {
			result.error = "dynamic body '" + body.id + "': invalid linearVelocity";
			return result;
		}
		result.settings.mLinearVelocity = ParsePhysicsFloatVec3(*body.linear_velocity);
	}
	if (body.angular_velocity) {
		if (!IsFiniteFloatVec(*body.angular_velocity, 3)) {
			result.error = "dynamic body '" + body.id + "': invalid angularVelocity";
			return result;
		}
		result.settings.mAngularVelocity = ParsePhysicsFloatVec3(*body.angular_velocity);
	}

	return result;
}

JPH::BodyCreationSettings PhysicsBodyAssetFactory::CreateBodySettingsFromPrototype(
	const PrototypeEntry& proto,
	JPH::RVec3Arg position,
	JPH::QuatArg rotation,
	uint64_t user_data) {
	JPH::BodyCreationSettings settings(
		proto.shape, position, NormalizePhysicsQuat(rotation), proto.motion_type, proto.object_layer);
	if (proto.motion_type != JPH::EMotionType::Static && proto.mass > 0.0f) {
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	}
	settings.mMassPropertiesOverride.mMass = proto.mass;
	settings.mFriction = proto.friction;
	settings.mRestitution = proto.restitution;
	settings.mLinearDamping = proto.linear_damping;
	settings.mAngularDamping = proto.angular_damping;
	settings.mGravityFactor = proto.gravity_factor;
	settings.mMotionQuality = proto.motion_quality;
	settings.mIsSensor = proto.is_sensor;
	settings.mAllowSleeping = proto.allow_sleeping;
	settings.mMaxLinearVelocity = proto.max_linear_velocity;
	settings.mMaxAngularVelocity = proto.max_angular_velocity;
	settings.mAllowedDOFs = JPH::EAllowedDOFs(proto.allowed_dofs);
	settings.mUserData = user_data;
	return settings;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
