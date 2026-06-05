#pragma once

// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// See physics_system.h for full documentation.
// Including this header without the macro will cause a compile-time #error.
#ifndef PHYSICS_INTERNAL_ACCESS
#error \
	"physics_diff.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

#include "runtime/physics/physics_commands.h"
#include "runtime/physics/physics_config.h"

namespace engine {

// BodyStateSnapshot — per-body state captured each frame for diff generation

struct BodyStateSnapshot {
	JPH::RVec3 position = JPH::RVec3::sZero();
	JPH::Quat rotation = JPH::Quat::sIdentity();
	JPH::Vec3 linear_velocity = JPH::Vec3::sZero();
	JPH::Vec3 angular_velocity = JPH::Vec3::sZero();
};

// GenerateDiff — compare current vs previous state, produce DiffPacket
//
// Returns std::nullopt if no change exceeds the configured thresholds.
// Otherwise returns a DiffPacket with the changed fields packed.
// Uses per-field epsilon from ThresholdsConfig [D24].

std::optional<DiffPacket> GenerateDiff(uint32_t body_id,
									   const BodyStateSnapshot& current,
									   const BodyStateSnapshot& previous,
									   const ThresholdsConfig& thresholds);

// ObjectRegistry — body_id ↔ asset_name bidirectional mapping
//
// Static bodies: registered during asset loading.
// Dynamic bodies: registered at Spawn time.

class ObjectRegistry {
	public:
	struct Entry {
		uint32_t body_id = 0;
		std::string asset_name;
	};

	// Register a mapping. asset_name may be empty for dynamic spawns.
	void Register(uint32_t body_id, const std::string& asset_name);

	// Remove a mapping (called when body is destroyed).
	void Unregister(uint32_t body_id);

	// Lookup asset name by body_id. Returns empty string if not found.
	const std::string& GetAssetName(uint32_t body_id) const;

	// Lookup body_id by asset name.
	std::optional<uint32_t> GetBodyId(const std::string& asset_name) const;

	// Check if a body_id is registered.
	bool Has(uint32_t body_id) const;

	// Clear all registrations.
	void Clear();

	// Remove mappings for body ids that are no longer present in the physics system.
	void PruneMissing(const std::unordered_set<uint32_t>& live_body_ids);

	size_t Size() const {
		return id_to_name_.size();
	}

	// Return all registered mappings sorted by body_id for deterministic iteration.
	std::vector<Entry> Entries() const;

	private:
	std::unordered_map<uint32_t, std::string> id_to_name_;
	std::unordered_map<std::string, uint32_t> name_to_id_;
	static inline const std::string kEmptyString;
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
