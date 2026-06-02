#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/EActivation.h>

#include "runtime/core/engine_api.h"
#include "runtime/physics/physics_asset_common.h"
#include "runtime/physics/physics_materials.h"
#include "runtime/physics/physics_shape_assets.h"

namespace engine {

struct CLOUD_ENGINE_API PrototypeEntry {
	std::string proto_id;
	JPH::RefConst<JPH::Shape> shape;
	float mass = 1.0f;
	float friction = 0.2f;
	float restitution = 0.0f;
	JPH::EMotionType motion_type = JPH::EMotionType::Dynamic;
	JPH::EMotionQuality motion_quality = JPH::EMotionQuality::Discrete;
	float linear_damping = 0.05f;
	float angular_damping = 0.05f;
	float gravity_factor = 1.0f;
	JPH::ObjectLayer object_layer = 1;
	uint8_t allowed_dofs = 0b111111;
	bool is_sensor = false;
	bool allow_sleeping = true;
	float max_linear_velocity = 500.0f;
	float max_angular_velocity = 47.1f;
};

struct CLOUD_ENGINE_API JsonStaticBody {
	std::string id;
	JsonShapeDef shape;
	JsonMaterial material;
	JsonTransform transform;
	std::string object_layer = "static";
};

struct CLOUD_ENGINE_API JsonDynamicPrototype {
	std::string proto_id;
	JsonShapeDef shape;
	float mass = 1.0f;
	JsonMaterial material;
	std::string motion_type = "dynamic";
	std::string motion_quality = "discrete";
	float linear_damping = 0.05f;
	float angular_damping = 0.05f;
	float gravity_factor = 1.0f;
	std::string object_layer = "dynamic";
	std::vector<uint8_t> allowed_dofs = {0, 1, 2, 3, 4, 5};
	bool is_sensor = false;
	bool allow_sleeping = true;
	float max_linear_velocity = 500.0f;
	float max_angular_velocity = 47.1f;
};

struct CLOUD_ENGINE_API JsonDynamicBody {
	std::string id;
	std::string proto_id;
	JsonTransform transform;
	uint64_t user_data = 0;
	bool activate = true;
	std::optional<std::vector<float>> linear_velocity;
	std::optional<std::vector<float>> angular_velocity;
};

struct CLOUD_ENGINE_API PrototypeBuildResult {
	PrototypeEntry prototype;
	std::string error;
};

struct CLOUD_ENGINE_API BodySettingsResult {
	JPH::BodyCreationSettings settings;
	std::string id;
	std::string source_proto_id;
	JPH::EActivation activation = JPH::EActivation::Activate;
	bool dynamic = false;
	std::string error;
};

class CLOUD_ENGINE_API PhysicsBodyAssetFactory {
	public:
	static PrototypeBuildResult BuildPrototype(const JsonDynamicPrototype& proto,
											   const MaterialTable& material_table,
											   const LayerConfig& layer_config,
											   const std::string& assets_dir);

	static BodySettingsResult BuildStaticBody(const JsonStaticBody& body,
											  const MaterialTable& material_table,
											  const LayerConfig& layer_config,
											  const std::string& assets_dir);

	static BodySettingsResult BuildDynamicBody(
		const JsonDynamicBody& body,
		const std::unordered_map<std::string, PrototypeEntry>& prototypes);

	static JPH::BodyCreationSettings CreateBodySettingsFromPrototype(const PrototypeEntry& proto,
																	 JPH::RVec3Arg position,
																	 JPH::QuatArg rotation,
																	 uint64_t user_data = 0);
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
