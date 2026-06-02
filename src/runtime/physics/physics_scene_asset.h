#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <optional>
#include <string>
#include <vector>

#include "runtime/core/engine_api.h"
#include "runtime/physics/physics_body_assets.h"
#include "runtime/physics/physics_constraint_assets.h"
#include "runtime/physics/physics_materials.h"

namespace engine {

struct PhysicsSceneAssetLoadResult;

struct CLOUD_ENGINE_API PhysicsSceneAsset {
	std::vector<MaterialEntry> materials;
	std::vector<JsonStaticBody> static_bodies;
	std::vector<JsonDynamicPrototype> dynamic_prototypes;
	std::vector<JsonDynamicBody> dynamic_bodies;
	std::vector<JsonConstraint> constraints;
	std::string source_path;
	std::string assets_dir;

	static PhysicsSceneAssetLoadResult LoadFromFile(const std::string& json_path);
	static PhysicsSceneAssetLoadResult LoadFromJson(const std::string& json,
													const std::string& source_path = {});
};

struct CLOUD_ENGINE_API PhysicsSceneAssetLoadResult {
	PhysicsSceneAsset scene;
	std::string error;
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
