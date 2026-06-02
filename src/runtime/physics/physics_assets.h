#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "runtime/core/engine_api.h"
#include "runtime/physics/physics_asset_common.h"
#include "runtime/physics/physics_body_assets.h"
#include "runtime/physics/physics_constraint_assets.h"
#include "runtime/physics/physics_scene_asset.h"
#include "runtime/physics/physics_shape_assets.h"

namespace engine {

class CLOUD_ENGINE_API AssetLoader {
	public:
	using ShapeCreateResult = engine::ShapeCreateResult;

	AssetLoadResult LoadScene(const std::string& json_path,
							  JPH::BodyInterface& body_interface,
							  JPH::PhysicsSystem& physics_system,
							  const MaterialTable& material_table,
							  const LayerConfig& layer_config);

	const std::unordered_map<std::string, PrototypeEntry>& GetPrototypes() const {
		return prototypes_;
	}

	const std::unordered_map<uint32_t, std::string>& GetStaticBodyIds() const {
		return static_body_ids_;
	}

	const std::vector<PhysicsSceneBodyRecord>& GetSceneBodyRecords() const {
		return scene_body_records_;
	}

	ShapeCreateResult CreateShape(const JsonShapeDef& def,
								  const MaterialTable& material_table,
								  const std::string& assets_dir = {});

	static JPH::RVec3 ParseVec3(const std::vector<double>& v);
	static JPH::Quat ParseQuat(const std::vector<float>& q);

	private:
	std::unordered_map<std::string, PrototypeEntry> prototypes_;
	std::unordered_map<uint32_t, std::string> static_body_ids_;
	std::vector<PhysicsSceneBodyRecord> scene_body_records_;
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
