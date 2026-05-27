#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Core/Color.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

#include "runtime/core/engine_api.h"

namespace engine {

//============================================================================
// PhysicsMaterialSimple — minimal physics material with friction & restitution
//
// This is the project-level material that stores surface properties.
// For simple bodies, friction/restitution are set directly on
// BodyCreationSettings. This class serves MeshShape/HeightFieldShape
// per-face material lists.
//============================================================================

class PhysicsMaterialSimple final : public JPH::PhysicsMaterial {
	public:
	PhysicsMaterialSimple() = default;

	PhysicsMaterialSimple(const std::string& name, float friction, float restitution)
		: name_(name), friction_(friction), restitution_(restitution) {
	}

	const char* GetDebugName() const override {
		return name_.c_str();
	}
	JPH::Color GetDebugColor() const override {
		return JPH::Color(static_cast<JPH::uint8>(friction_ * 255.0f),
						  0,
						  static_cast<JPH::uint8>((1.0f - friction_) * 255.0f));
	}

	const std::string& GetName() const {
		return name_;
	}
	float GetFriction() const {
		return friction_;
	}
	float GetRestitution() const {
		return restitution_;
	}

	using Ref = JPH::Ref<PhysicsMaterialSimple>;
	using RefConst = JPH::RefConst<PhysicsMaterialSimple>;

	private:
	std::string name_ = "default";
	float friction_ = 0.2f;
	float restitution_ = 0.0f;
};

//============================================================================
// MaterialTable — named material registry loaded from JSON
//============================================================================

struct MaterialEntry {
	std::string name;
	float friction = 0.2f;
	float restitution = 0.0f;
};

class ENGINE_API MaterialTable {
	public:
	// Parse materials from a JSON array of MaterialEntry objects.
	// Uses glaze for deserialization.
	bool LoadFromJson(const std::string& json);

	// Register materials from a vector of entries.
	void Register(const std::vector<MaterialEntry>& entries);

	// Lookup a material by name. Returns sDefault if not found.
	JPH::RefConst<JPH::PhysicsMaterial> Get(const std::string& name) const;

	// Get all registered material names.
	std::vector<std::string> GetNames() const;

	// Create a material list (for MeshShape/HeightFieldShape) from names.
	// Returns one RefConst per name; uses a default material if name not found.
	JPH::PhysicsMaterialList CreateList(const std::vector<std::string>& names) const;

	// Clear all registered materials.
	void Clear();

	size_t Size() const {
		return materials_.size();
	}
	bool Empty() const {
		return materials_.empty();
	}

	private:
	// Hold ownership via Ref so materials don't get freed while in use.
	std::unordered_map<std::string, JPH::Ref<PhysicsMaterialSimple>> owned_;
	std::unordered_map<std::string, JPH::RefConst<PhysicsMaterialSimple>> materials_;
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
