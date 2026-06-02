#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <optional>
#include <string>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <glaze/json.hpp>

#include "runtime/core/engine_api.h"
#include "runtime/physics/physics_materials.h"

namespace engine {

struct CLOUD_ENGINE_API JsonShapeDef {
	std::string type;
	glz::generic params;
	std::optional<std::vector<JsonShapeDef>> shapes;
	std::optional<std::vector<double>> position;
	std::optional<std::vector<float>> rotation;
	std::optional<std::string> material;
};

struct CLOUD_ENGINE_API ShapeCreateResult {
	JPH::RefConst<JPH::Shape> shape;
	std::string error;
};

class CLOUD_ENGINE_API PhysicsShapeAssetFactory {
	public:
	static ShapeCreateResult CreateShape(const JsonShapeDef& def,
										 const MaterialTable& material_table,
										 const std::string& assets_dir = {});
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
