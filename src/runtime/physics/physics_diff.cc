#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_diff.h"

#include <cmath>

#include "runtime/profiler/profiler_events.h"

namespace engine {

// GenerateDiff

std::optional<DiffPacket> GenerateDiff(uint32_t body_id,
									   const BodyStateSnapshot& current,
									   const BodyStateSnapshot& previous,
									   const ThresholdsConfig& thresholds) {
	ENGINE_PROFILE_SCOPE("engine.physics", "GenerateDiff");
	DiffPacket packet;
	packet.object_id = body_id;
	packet.change_mask = 0;

	// Bit 0: position check — in RVec3 (double precision)
	JPH::RVec3 pos_delta = current.position - previous.position;
	if (pos_delta.LengthSq() > thresholds.position_epsilon * thresholds.position_epsilon) {
		packet.change_mask |= (1 << 0);
	}

	// Bit 1: rotation check — angle between quaternions
	// Quaternion angle = 2 * acos(|dot(q1, q2)|)
	// For small angles: angle ≈ 2 * sqrt(1 - |dot|^2)
	float dot = std::abs(current.rotation.GetX() * previous.rotation.GetX() +
						 current.rotation.GetY() * previous.rotation.GetY() +
						 current.rotation.GetZ() * previous.rotation.GetZ() +
						 current.rotation.GetW() * previous.rotation.GetW());
	// Clamp for numerical stability
	if (dot > 1.0f) dot = 1.0f;
	float angle = 2.0f * std::acos(dot);
	if (angle > thresholds.rotation_epsilon) {
		packet.change_mask |= (1 << 1);
	}

	// Bit 2: linear velocity check
	JPH::Vec3 lv_delta = current.linear_velocity - previous.linear_velocity;
	if (lv_delta.LengthSq() >
		thresholds.linear_velocity_epsilon * thresholds.linear_velocity_epsilon) {
		packet.change_mask |= (1 << 2);
	}

	// Bit 3: angular velocity check
	JPH::Vec3 av_delta = current.angular_velocity - previous.angular_velocity;
	if (av_delta.LengthSq() >
		thresholds.angular_velocity_epsilon * thresholds.angular_velocity_epsilon) {
		packet.change_mask |= (1 << 3);
	}

	// No changes — return nullopt
	if (packet.change_mask == 0) {
		return std::nullopt;
	}

	// Fill values array in mask bit order
	if (packet.change_mask & (1 << 0)) {
		// Position: 3 floats (converted from double)
		packet.values.push_back(static_cast<float>(current.position.GetX()));
		packet.values.push_back(static_cast<float>(current.position.GetY()));
		packet.values.push_back(static_cast<float>(current.position.GetZ()));
	}
	if (packet.change_mask & (1 << 1)) {
		// Rotation: 4 floats (quaternion xyzw)
		packet.values.push_back(current.rotation.GetX());
		packet.values.push_back(current.rotation.GetY());
		packet.values.push_back(current.rotation.GetZ());
		packet.values.push_back(current.rotation.GetW());
	}
	if (packet.change_mask & (1 << 2)) {
		// Linear velocity: 3 floats
		packet.values.push_back(current.linear_velocity.GetX());
		packet.values.push_back(current.linear_velocity.GetY());
		packet.values.push_back(current.linear_velocity.GetZ());
	}
	if (packet.change_mask & (1 << 3)) {
		// Angular velocity: 3 floats
		packet.values.push_back(current.angular_velocity.GetX());
		packet.values.push_back(current.angular_velocity.GetY());
		packet.values.push_back(current.angular_velocity.GetZ());
	}

	return packet;
}

// ObjectRegistry

void ObjectRegistry::Register(uint32_t body_id, const std::string& asset_name) {
	ENGINE_PROFILE_SCOPE("engine.physics", "ObjRegistryRegister");
	if (!asset_name.empty()) {
		auto name_it = name_to_id_.find(asset_name);
		if (name_it != name_to_id_.end() && name_it->second != body_id) {
			id_to_name_.erase(name_it->second);
		}
	}
	// If this body_id was already registered with a different name,
	// remove the stale name-to-id mapping before overwriting.
	auto it = id_to_name_.find(body_id);
	if (it != id_to_name_.end() && !it->second.empty() && it->second != asset_name) {
		name_to_id_.erase(it->second);
	}
	id_to_name_[body_id] = asset_name;
	if (!asset_name.empty()) {
		name_to_id_[asset_name] = body_id;
	}
}

void ObjectRegistry::Unregister(uint32_t body_id) {
	ENGINE_PROFILE_SCOPE("engine.physics", "ObjRegistryUnregister");
	auto it = id_to_name_.find(body_id);
	if (it != id_to_name_.end()) {
		if (!it->second.empty()) {
			name_to_id_.erase(it->second);
		}
		id_to_name_.erase(it);
	}
}

const std::string& ObjectRegistry::GetAssetName(uint32_t body_id) const {
	auto it = id_to_name_.find(body_id);
	if (it != id_to_name_.end()) {
		return it->second;
	}
	return kEmptyString;
}

std::optional<uint32_t> ObjectRegistry::GetBodyId(const std::string& asset_name) const {
	auto it = name_to_id_.find(asset_name);
	if (it != name_to_id_.end()) {
		return it->second;
	}
	return std::nullopt;
}

bool ObjectRegistry::Has(uint32_t body_id) const {
	return id_to_name_.find(body_id) != id_to_name_.end();
}

void ObjectRegistry::Clear() {
	id_to_name_.clear();
	name_to_id_.clear();
}

void ObjectRegistry::PruneMissing(const std::unordered_set<uint32_t>& live_body_ids) {
	for (auto it = id_to_name_.begin(); it != id_to_name_.end();) {
		if (live_body_ids.find(it->first) == live_body_ids.end()) {
			if (!it->second.empty()) {
				name_to_id_.erase(it->second);
			}
			it = id_to_name_.erase(it);
		} else {
			++it;
		}
	}
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
