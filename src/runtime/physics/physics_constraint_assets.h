#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "runtime/core/engine_api.h"

namespace engine {

struct CLOUD_ENGINE_API JsonConstraint {
	std::string type;
	std::string body_a;
	std::string body_b;
	std::vector<double> pivot = {0.0, 0.0, 0.0};
	std::vector<double> axis = {0.0, 1.0, 0.0};
	std::optional<std::vector<double>> axis2;

	struct CLOUD_ENGINE_API ConstraintLimits {
		double min = -3.14159;
		double max = 3.14159;
	} limits;

	struct CLOUD_ENGINE_API ConstraintSpring {
		float frequency = 1.0f;
		float damping = 0.5f;
	} spring;
};

struct CLOUD_ENGINE_API ConstraintCreateResult {
	bool success = false;
	JPH::TwoBodyConstraint* constraint = nullptr;
	std::string error;
};

class CLOUD_ENGINE_API PhysicsConstraintAssetFactory {
	public:
	using BodyLookup = std::unordered_map<std::string, JPH::BodyID>;

	static ConstraintCreateResult CreateAndAddConstraint(const JsonConstraint& constraint,
														 const BodyLookup& bodies,
														 JPH::BodyInterface& body_interface,
														 JPH::PhysicsSystem& physics_system);
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
