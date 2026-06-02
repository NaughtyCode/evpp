#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_constraint_assets.h"

#include <cmath>

#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include "runtime/physics/physics_asset_common.h"

namespace engine {

namespace {

bool ResolveConstraintBody(const std::string& name,
						   const PhysicsConstraintAssetFactory::BodyLookup& bodies,
						   JPH::BodyID& out_id) {
	if (name == "world" || name == "fixed" || name == "__world__") {
		out_id = JPH::BodyID();
		return true;
	}
	auto it = bodies.find(name);
	if (it == bodies.end()) {
		return false;
	}
	out_id = it->second;
	return true;
}

}  // namespace

ConstraintCreateResult PhysicsConstraintAssetFactory::CreateAndAddConstraint(
	const JsonConstraint& con,
	const BodyLookup& bodies,
	JPH::BodyInterface& body_interface,
	JPH::PhysicsSystem& physics_system) {
	ConstraintCreateResult result;

	JPH::BodyID ja;
	JPH::BodyID jb;
	if (!ResolveConstraintBody(con.body_a, bodies, ja) ||
		!ResolveConstraintBody(con.body_b, bodies, jb)) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b +
					   "': referenced body not found";
		return result;
	}
	if (ja.IsInvalid() && jb.IsInvalid()) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b +
					   "': at least one side must reference a body";
		return result;
	}
	if (!ja.IsInvalid() && !jb.IsInvalid() && ja == jb) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b +
					   "': cannot constrain a body to itself";
		return result;
	}

	if (!IsFiniteDoubleVec(con.pivot, 3) || !IsFiniteDoubleVec(con.axis, 3) ||
		!std::isfinite(con.limits.min) || !std::isfinite(con.limits.max) ||
		con.limits.min > con.limits.max || !std::isfinite(con.spring.frequency) ||
		con.spring.frequency < 0.0f || !std::isfinite(con.spring.damping) ||
		con.spring.damping < 0.0f) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b +
					   "': invalid pivot/axis/limits/spring";
		return result;
	}

	JPH::RVec3 pivot = ParsePhysicsVec3(con.pivot);
	JPH::Vec3 axis(static_cast<float>(con.axis[0]),
				   static_cast<float>(con.axis[1]),
				   static_cast<float>(con.axis[2]));
	if (axis.LengthSq() <= 1.0e-12f) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b +
					   "': axis must be non-zero";
		return result;
	}
	axis = axis.Normalized();

	JPH::TwoBodyConstraint* created = nullptr;
	if (con.type == "hinge") {
		JPH::HingeConstraintSettings settings;
		settings.mPoint1 = settings.mPoint2 = pivot;
		settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
		settings.mLimitsMin = static_cast<float>(con.limits.min);
		settings.mLimitsMax = static_cast<float>(con.limits.max);
		created = body_interface.CreateConstraint(&settings, ja, jb);
	} else if (con.type == "spring") {
		JPH::DistanceConstraintSettings settings;
		settings.mPoint1 = settings.mPoint2 = pivot;
		settings.mLimitsSpringSettings.mFrequency = con.spring.frequency;
		settings.mLimitsSpringSettings.mDamping = con.spring.damping;
		created = body_interface.CreateConstraint(&settings, ja, jb);
	} else if (con.type == "slider") {
		JPH::SliderConstraintSettings settings;
		settings.mPoint1 = pivot;
		settings.mPoint2 = pivot;
		settings.mSliderAxis1 = axis;
		settings.mLimitsMin = static_cast<float>(con.limits.min);
		settings.mLimitsMax = static_cast<float>(con.limits.max);
		if (con.axis2.has_value()) {
			if (!IsFiniteDoubleVec(*con.axis2, 3)) {
				result.error = "slider constraint axis2 must contain 3 finite numbers";
				return result;
			}
			settings.mSliderAxis2 = JPH::Vec3(static_cast<float>((*con.axis2)[0]),
											  static_cast<float>((*con.axis2)[1]),
											  static_cast<float>((*con.axis2)[2]));
			if (settings.mSliderAxis2.LengthSq() <= 1.0e-12f) {
				result.error = "slider constraint axis2 must be non-zero";
				return result;
			}
			settings.mSliderAxis2 = settings.mSliderAxis2.Normalized();
		} else {
			settings.mSliderAxis2 = axis;
		}
		created = body_interface.CreateConstraint(&settings, ja, jb);
	} else if (con.type == "fixed") {
		JPH::FixedConstraintSettings settings;
		settings.mPoint1 = settings.mPoint2 = pivot;
		created = body_interface.CreateConstraint(&settings, ja, jb);
	} else {
		result.error = "unknown constraint type '" + con.type +
					   "'. Supported: hinge, spring, slider, fixed";
		return result;
	}

	if (!created) {
		result.error = "constraint '" + con.body_a + "' -> '" + con.body_b + "': creation failed";
		return result;
	}

	physics_system.AddConstraint(created);
	result.success = true;
	result.constraint = created;
	return result;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
