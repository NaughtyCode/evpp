#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Body/MotionQuality.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "runtime/core/engine_api.h"
#include "runtime/physics/physics_config.h"

namespace engine {

struct CLOUD_ENGINE_API JsonMaterial {
	float friction = 0.2f;
	float restitution = 0.0f;
};

struct CLOUD_ENGINE_API JsonTransform {
	std::vector<double> position = {0.0, 0.0, 0.0};
	std::vector<float> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct CLOUD_ENGINE_API PhysicsSceneBodyRecord {
	uint32_t body_id = 0;
	std::string asset_id;
	JPH::RVec3 position = JPH::RVec3::sZero();
	JPH::Quat rotation = JPH::Quat::sIdentity();
	JPH::Vec3 linear_velocity = JPH::Vec3::sZero();
	JPH::Vec3 angular_velocity = JPH::Vec3::sZero();
	bool dynamic = false;
};

struct CLOUD_ENGINE_API AssetLoadResult {
	bool success = false;
	std::string error;
	int static_bodies_loaded = 0;
	int dynamic_prototypes_loaded = 0;
	int dynamic_bodies_loaded = 0;
	int constraints_loaded = 0;
	int scene_objects_loaded = 0;
};

JPH::RVec3 ParsePhysicsVec3(const std::vector<double>& v);
JPH::Vec3 ParsePhysicsFloatVec3(const std::vector<float>& v);
JPH::Quat ParsePhysicsQuat(const std::vector<float>& q);
JPH::Quat NormalizePhysicsQuat(JPH::QuatArg q);

bool IsPositiveFinite(double value);
bool IsFiniteFloat(float value);
bool IsFiniteDoubleVec(const std::vector<double>& values, size_t expected);
bool IsFiniteFloatVec(const std::vector<float>& values, size_t expected);
bool IsMaterialValid(const JsonMaterial& material);

JPH::EMotionType ParseMotionType(const std::string& s);
bool IsMotionTypeName(const std::string& s);
JPH::EMotionQuality ParseMotionQuality(const std::string& s);
bool IsMotionQualityName(const std::string& s);

bool BuildAllowedDofs(const std::vector<uint8_t>& dofs, uint8_t& out_mask, std::string& out_error);
bool ResolveObjectLayer(const LayerConfig& layer_config,
						const std::string& name,
						JPH::ObjectLayer& out_layer,
						std::string& out_error);

std::string GetAssetDirectory(const std::string& asset_path);
std::string ResolveAssetPath(const std::string& assets_dir, const std::string& path);

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
