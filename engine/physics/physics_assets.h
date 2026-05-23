#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>

#include "engine/physics/physics_materials.h"

namespace JPH {
class PhysicsSystem;
class Body;
}

namespace engine {

struct PhysicsConfig;

//============================================================================
// PrototypeEntry — a dynamic body template stored in the prototype pool
//============================================================================

struct PrototypeEntry {
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
    uint16_t object_layer = 1;  // default: "dynamic"
    uint8_t allowed_dofs = 0b111111;  // all 6 DOFs
    bool is_sensor = false;
    bool allow_sleeping = true;
    float max_linear_velocity = 500.0f;
    float max_angular_velocity = 47.1f;
};

//============================================================================
// AssetLoadResult — outcome of loading a physics asset file
//============================================================================

struct AssetLoadResult {
    bool success = false;
    std::string error;           // empty on success
    int static_bodies_loaded = 0;
    int dynamic_prototypes_loaded = 0;
    int constraints_loaded = 0;
};

//============================================================================
// AssetLoader — loads scene assets from JSON files into the physics world
//
// References JPH::BodyInterface for body creation (received as parameter,
// avoiding circular dependency with PhysicsWorld).
//============================================================================

class AssetLoader {
public:
    // Load a complete scene asset file.
    // body_interface: the Jolt BodyInterface for body creation.
    // material_table: pre-registered material definitions.
    // layer_config: for ObjectLayer name → value resolution.
    // Returns result with error details on failure [D22].
    AssetLoadResult LoadScene(
        const std::string& json_path,
        JPH::BodyInterface& body_interface,
        const MaterialTable& material_table,
        const LayerConfig& layer_config);

    // Read-only access to loaded data (for use by PhysicsWorld).
    const std::unordered_map<std::string, PrototypeEntry>& GetPrototypes() const {
        return prototypes_;
    }
    const std::unordered_map<uint32_t, std::string>& GetStaticBodyIds() const {
        return static_body_ids_;
    }

private:
    // Internal: create a JPH::ShapeSettings from JSON shape description.
    // Supports single shape or array of shapes (compound).
    struct ShapeCreateResult {
        JPH::RefConst<JPH::Shape> shape;
        std::string error;
    };

    // Forward declaration of internal helper struct
    struct JsonShapeDef;

    ShapeCreateResult CreateShape(const JsonShapeDef& def,
                                   const MaterialTable& material_table);

    // Parse JPH::RVec3 from JSON array [x, y, z]
    static JPH::RVec3 ParseVec3(const std::vector<double>& v);
    // Parse JPH::Quat from JSON array [x, y, z, w]
    static JPH::Quat ParseQuat(const std::vector<float>& q);

    std::unordered_map<std::string, PrototypeEntry> prototypes_;
    std::unordered_map<uint32_t, std::string> static_body_ids_;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
