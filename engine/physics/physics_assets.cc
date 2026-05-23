#ifdef ENGINE_PHYSICS_ENABLED

#include "engine/physics/physics_assets.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <glaze/json.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>

namespace engine {

//============================================================================
// Internal JSON parsing structures (used with glaze for asset deserialization)
//============================================================================

// ── Material JSON shape ──
struct JsonMaterial {
    float friction = 0.2f;
    float restitution = 0.0f;
};

// ── Transform in JSON ──
struct JsonTransform {
    std::vector<double> position = {0.0, 0.0, 0.0};
    std::vector<float> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

// ── Static body entry ──
struct JsonStaticBody {
    std::string id;
    JsonShapeDef shape;
    JsonMaterial material;
    JsonTransform transform;
    std::string object_layer = "static";
};

// ── Dynamic prototype entry ──
struct JsonDynamicPrototype {
    std::string proto_id;
    JsonShapeDef shape;
    float mass = 1.0f;
    JsonMaterial material;
    std::string motion_type = "dynamic";
    std::string motion_quality = "discrete";
    float linear_damping = 0.05f;
    float angular_damping = 0.05f;
    float gravity_factor = 1.0f;
    std::string object_layer = "dynamic";
    std::vector<uint8_t> allowed_dofs = {0, 1, 2, 3, 4, 5};  // all 6
    bool is_sensor = false;
    bool allow_sleeping = true;
    float max_linear_velocity = 500.0f;
    float max_angular_velocity = 47.1f;
};

// ── Constraint entry ──
struct JsonConstraint {
    std::string type;         // "hinge", "spring", "slider", "fixed"
    std::string body_a;       // asset id
    std::string body_b;       // asset id
    std::vector<double> pivot = {0.0, 0.0, 0.0};
    std::vector<double> axis = {0.0, 1.0, 0.0};
    std::optional<std::vector<double>> axis2;  // for slider
    struct ConstraintLimits {
        double min = -3.14159;
        double max = 3.14159;
    } limits;
    struct ConstraintSpring {
        float frequency = 1.0f;
        float damping = 0.5f;
    } spring;  // for "spring" type constraints
};

// ── Full asset file schema ──
struct JsonAssetFile {
    std::optional<std::vector<JsonStaticBody>> static_bodies;
    std::optional<std::vector<JsonDynamicPrototype>> dynamic_prototypes;
    std::optional<std::vector<JsonConstraint>> constraints;
    std::optional<std::vector<MaterialEntry>> materials;  // inline material defs
};

//============================================================================
// glaze reflection for JSON asset structures
//============================================================================

} // namespace engine

template <>
struct glz::meta<engine::JsonMaterial> {
    using T = engine::JsonMaterial;
    static constexpr auto value = glz::object(
        "friction", &T::friction,
        "restitution", &T::restitution
    );
};

template <>
struct glz::meta<engine::JsonTransform> {
    using T = engine::JsonTransform;
    static constexpr auto value = glz::object(
        "position", &T::position,
        "rotation", &T::rotation
    );
};

template <>
struct glz::meta<engine::JsonStaticBody> {
    using T = engine::JsonStaticBody;
    static constexpr auto value = glz::object(
        "id", &T::id,
        "shape", &T::shape,
        "material", &T::material,
        "transform", &T::transform,
        "objectLayer", &T::object_layer
    );
};

template <>
struct glz::meta<engine::JsonDynamicPrototype> {
    using T = engine::JsonDynamicPrototype;
    static constexpr auto value = glz::object(
        "protoId", &T::proto_id,
        "shape", &T::shape,
        "mass", &T::mass,
        "material", &T::material,
        "motionType", &T::motion_type,
        "motionQuality", &T::motion_quality,
        "linearDamping", &T::linear_damping,
        "angularDamping", &T::angular_damping,
        "gravityFactor", &T::gravity_factor,
        "objectLayer", &T::object_layer,
        "allowedDofs", &T::allowed_dofs,
        "isSensor", &T::is_sensor,
        "allowSleeping", &T::allow_sleeping,
        "maxLinearVelocity", &T::max_linear_velocity,
        "maxAngularVelocity", &T::max_angular_velocity
    );
};

template <>
struct glz::meta<engine::JsonConstraint> {
    using T = engine::JsonConstraint;
    static constexpr auto value = glz::object(
        "type", &T::type,
        "bodyA", &T::body_a,
        "bodyB", &T::body_b,
        "pivot", &T::pivot,
        "axis", &T::axis,
        "axis2", &T::axis2,
        "limits", &T::limits,
        "spring", &T::spring
    );

};

template <>
struct glz::meta<engine::JsonAssetFile> {
    using T = engine::JsonAssetFile;
    static constexpr auto value = glz::object(
        "staticBodies", &T::static_bodies,
        "dynamicPrototypes", &T::dynamic_prototypes,
        "constraints", &T::constraints,
        "materials", &T::materials
    );
};

namespace engine {

//============================================================================
// Helpers
//============================================================================

JPH::RVec3 AssetLoader::ParseVec3(const std::vector<double>& v) {
    return JPH::RVec3(
        v.size() > 0 ? v[0] : 0.0,
        v.size() > 1 ? v[1] : 0.0,
        v.size() > 2 ? v[2] : 0.0
    );
}

JPH::Quat AssetLoader::ParseQuat(const std::vector<float>& q) {
    if (q.size() >= 4) {
        return JPH::Quat(q[0], q[1], q[2], q[3]);
    }
    return JPH::Quat::sIdentity();
}

//============================================================================
// Shape creation from JSON definition
//============================================================================

AssetLoader::ShapeCreateResult AssetLoader::CreateShape(
    const JsonShapeDef& def, const MaterialTable& material_table) {

    ShapeCreateResult result;

    // Handle compound shape: array of sub-shapes
    if (def.shapes.has_value() && !def.shapes->empty()) {
        JPH::StaticCompoundShapeSettings compound;
        compound.mSubShapes.reserve(def.shapes->size());

        for (size_t i = 0; i < def.shapes->size(); ++i) {
            const auto& sub_def = (*def.shapes)[i];
            auto sub_result = CreateShape(sub_def, material_table);
            if (!sub_result.shape) {
                return sub_result;  // propagate error
            }

            JPH::RVec3 offset = JPH::RVec3::sZero();
            JPH::Quat rotation = JPH::Quat::sIdentity();
            if (sub_def.position) {
                offset = ParseVec3(*sub_def.position);
            }
            if (sub_def.rotation) {
                rotation = ParseQuat(*sub_def.rotation);
            }
            compound.AddShape(JPH::Vec3(offset), rotation, sub_result.shape);
        }

        JPH::ShapeSettings::ShapeResult sr = compound.Create();
        if (sr.IsValid()) {
            result.shape = sr.Get();
        } else {
            result.error = sr.GetError();
        }
        return result;
    }

    // Single shape — dispatch by type
    const auto& type = def.type;
    auto& p = const_cast<glz::generic&>(def.params);  // glz::generic (raw JSON object)

    // Helper: extract field from json_t
    auto get_num = [&](const std::string& key, double default_val = 0.0) -> double {
        if (p.contains(key)) {
            return p[key].template get<double>();
        }
        return default_val;
    };
    auto get_arr = [&](const std::string& key) -> glz::generic& {
        static glz::generic empty;
        if (p.contains(key) && p[key].is_array()) {
            return p[key];
        }
        return empty;
    };

    if (type == "box") {
        double hx = get_num("halfX", get_num("halfExtent", 0.5));
        double hy = get_num("halfY", get_num("halfExtent", 0.5));
        double hz = get_num("halfZ", get_num("halfExtent", 0.5));
        // Support array form: "halfExtent": [x, y, z]
        auto& he_arr = get_arr("halfExtent");
        if (he_arr.is_array() && he_arr.size() >= 3) {
            hx = he_arr[0u].template get<double>();
            hy = he_arr[1u].template get<double>();
            hz = he_arr[2u].template get<double>();
        }
        JPH::BoxShapeSettings settings(JPH::Vec3(
            static_cast<float>(hx), static_cast<float>(hy), static_cast<float>(hz)));
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "sphere") {
        double radius = get_num("radius", 0.5);
        JPH::SphereShapeSettings settings(static_cast<float>(radius));
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "capsule") {
        double half_height = get_num("halfHeight", 0.5);
        double radius = get_num("radius", 0.25);
        JPH::CapsuleShapeSettings settings(
            static_cast<float>(half_height), static_cast<float>(radius));
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "cylinder") {
        double half_height = get_num("halfHeight", 0.5);
        double radius = get_num("radius", 0.25);
        JPH::CylinderShapeSettings settings(
            static_cast<float>(half_height), static_cast<float>(radius));
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "convex_hull" || type == "convexHull") {
        JPH::Array<JPH::Vec3> points;
        auto& pts_arr = get_arr("points");
        if (pts_arr.is_array()) {
            for (size_t i = 0; i < pts_arr.size(); ++i) {
                auto& pt = pts_arr[unsigned(i)];
                if (pt.is_array() && pt.size() >= 3) {
                    points.emplace_back(
                        static_cast<float>(pt[0u].template get<double>()),
                        static_cast<float>(pt[1u].template get<double>()),
                        static_cast<float>(pt[2u].template get<double>()));
                }
            }
        }
        if (points.empty()) {
            result.error = "convex_hull requires non-empty 'points' array";
            return result;
        }
        JPH::ConvexHullShapeSettings settings(points);
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "mesh" || type == "mesh_shape") {
        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        auto& verts_arr = get_arr("vertices");
        if (verts_arr.is_array()) {
            for (size_t i = 0; i < verts_arr.size(); ++i) {
                auto& v = verts_arr[unsigned(i)];
                float x = static_cast<float>(v[0u].template get<double>());
                float y = static_cast<float>(v[1u].template get<double>());
                float z = static_cast<float>(v[2u].template get<double>());
                vertices.emplace_back(x, y, z);
            }
        }
        auto& tris_arr = get_arr("triangles");
        if (tris_arr.is_array()) {
            for (size_t i = 0; i < tris_arr.size(); ++i) {
                auto& t = tris_arr[unsigned(i)];
                triangles.emplace_back(
                    static_cast<uint32_t>(t[0u].template get<double>()),
                    static_cast<uint32_t>(t[1u].template get<double>()),
                    static_cast<uint32_t>(t[2u].template get<double>()));
            }
        }
        if (vertices.empty() || triangles.empty()) {
            result.error = "mesh requires 'vertices' and 'triangles' arrays";
            return result;
        }

        JPH::MeshShapeSettings settings(vertices, triangles);

        // Optional per-face materials
        auto& mats_arr = get_arr("materials");
        if (mats_arr.is_array()) {
            std::vector<std::string> mat_names;
            for (size_t i = 0; i < mats_arr.size(); ++i) {
                mat_names.push_back(mats_arr[unsigned(i)].template get<std::string>());
            }
            settings.mMaterials = material_table.CreateList(mat_names);
        }

        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "height_field" || type == "heightField") {
        JPH::HeightFieldShapeSettings settings;
        // Height field data is complex; read from an external binary file via path
        auto& df_arr = get_arr("dataFile");
        if (df_arr.is_array()) {
            // Path to binary data file specified in JSON; loading deferred
            result.error = "height_field requires external binary data file; "
                           "loading via dataFile path not yet implemented";
            return result;
        }
        // Fallback: parse inline samples
        auto& samples_arr = get_arr("samples");
        if (samples_arr.is_array()) {
            JPH::Array<float> samples;
            for (size_t i = 0; i < samples_arr.size(); ++i) {
                samples.push_back(static_cast<float>(samples_arr[unsigned(i)].template get<double>()));
            }
            uint32_t sample_count = static_cast<uint32_t>(get_num("sampleCount", 0));
            if (sample_count == 0) {
                sample_count = static_cast<uint32_t>(std::sqrt(samples.size()));
            }
            if (sample_count == 0 || sample_count > 65536) {
                result.error = "height_field sampleCount must be in [1, 65536]";
                return result;
            }
            settings.mHeightSamples = std::move(samples);
            settings.mSampleCount = sample_count;
        }
        // Offset and scale
        auto& off_arr = get_arr("offset");
        if (off_arr.is_array() && off_arr.size() >= 3) {
            settings.mOffset = JPH::Vec3(
                static_cast<float>(off_arr[0u].template get<double>()),
                static_cast<float>(off_arr[1u].template get<double>()),
                static_cast<float>(off_arr[2u].template get<double>()));
        }
        auto& scale_arr = get_arr("scale");
        if (scale_arr.is_array() && scale_arr.size() >= 3) {
            settings.mScale = JPH::Vec3(
                static_cast<float>(scale_arr[0u].template get<double>()),
                static_cast<float>(scale_arr[1u].template get<double>()),
                static_cast<float>(scale_arr[2u].template get<double>()));
        }
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else if (type == "plane") {
        JPH::Plane plane(JPH::Vec3::sAxisY(), 0.0f);
        auto& n_arr = get_arr("normal");
        if (n_arr.is_array() && n_arr.size() >= 3) {
            plane = JPH::Plane(
                JPH::Vec3(
                    static_cast<float>(n_arr[0u].template get<double>()),
                    static_cast<float>(n_arr[1u].template get<double>()),
                    static_cast<float>(n_arr[2u].template get<double>())),
                static_cast<float>(get_num("constant", 0.0)));
        }
        JPH::PlaneShapeSettings settings(plane);
        auto sr = settings.Create();
        if (sr.IsValid()) { result.shape = sr.Get(); } else { result.error = sr.GetError(); }
    }
    else {
        result.error = "unknown shape type: '" + type +
                       "'. Supported: box, sphere, capsule, cylinder, "
                       "convex_hull, mesh, height_field, plane, compound";
    }

    return result;
}

//============================================================================
// Parse motion type and quality from strings
//============================================================================

static JPH::EMotionType ParseMotionType(const std::string& s) {
    if (s == "static") return JPH::EMotionType::Static;
    if (s == "kinematic") return JPH::EMotionType::Kinematic;
    return JPH::EMotionType::Dynamic;
}

static JPH::EMotionQuality ParseMotionQuality(const std::string& s) {
    if (s == "linear_cast" || s == "linearCast") return JPH::EMotionQuality::LinearCast;
    return JPH::EMotionQuality::Discrete;
}

//============================================================================
// LoadScene — main entry point
//============================================================================

AssetLoadResult AssetLoader::LoadScene(
    const std::string& json_path,
    JPH::BodyInterface& body_interface,
    JPH::PhysicsSystem& physics_system,
    const MaterialTable& material_table,
    const LayerConfig& layer_config) {

    AssetLoadResult result;
    prototypes_.clear();
    static_body_ids_.clear();

    // Read file
    std::ifstream f(json_path, std::ios::binary);
    if (!f) {
        result.error = "asset file not found: " + json_path;
        return result;
    }
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    // Parse JSON
    JsonAssetFile asset;
    auto ec = glz::read_json(asset, json);
    if (ec) {
        result.error = "parse error in " + json_path + ": " +
                       glz::format_error(ec, json);
        return result;
    }

    // Load inline materials into a combined table
    MaterialTable combined_materials = material_table;
    if (asset.materials.has_value()) {
        combined_materials.Register(*asset.materials);
    }

    // ── Load static bodies ─────────────────────────────────────────────
    if (asset.static_bodies.has_value()) {
        // First pass: create bodies and collect BodyIDs for batch addition
        std::vector<JPH::BodyID> body_ids;
        body_ids.reserve(asset.static_bodies->size());

        for (const auto& sbody : *asset.static_bodies) {
            // Resolve object layer
            auto layer_it = layer_config.object_layers.find(sbody.object_layer);
            JPH::ObjectLayer obj_layer = (layer_it != layer_config.object_layers.end())
                ? JPH::ObjectLayer(layer_it->second) : JPH::ObjectLayer(0);

            // Create shape
            auto shape_result = CreateShape(sbody.shape, combined_materials);
            if (!shape_result.shape) {
                result.error = "static body '" + sbody.id + "': " + shape_result.error;
                return result;
            }

            JPH::RVec3 pos = ParseVec3(sbody.transform.position);
            JPH::Quat rot = ParseQuat(sbody.transform.rotation);

            JPH::BodyCreationSettings settings(
                shape_result.shape, pos, rot, JPH::EMotionType::Static, obj_layer);
            settings.mFriction = sbody.material.friction;
            settings.mRestitution = sbody.material.restitution;

            JPH::Body* body = body_interface.CreateBody(settings);
            if (!body) {
                result.error = "static body '" + sbody.id + "': creation failed";
                return result;
            }

            JPH::BodyID bid = body->GetID();
            body_ids.push_back(bid);

            uint32_t body_id = bid.GetIndexAndSequenceNumber();
            static_body_ids_[body_id] = sbody.id;
            ++result.static_bodies_loaded;
        }

        // Batch add all static bodies to the broad phase
        if (!body_ids.empty()) {
            JPH::BodyInterface::AddState add_state = body_interface.AddBodiesPrepare(
                body_ids.data(), static_cast<int>(body_ids.size()));
            body_interface.AddBodiesFinalize(
                body_ids.data(), static_cast<int>(body_ids.size()),
                add_state, JPH::EActivation::DontActivate);
        }
    }

    // ── Load dynamic prototypes ────────────────────────────────────────
    if (asset.dynamic_prototypes.has_value()) {
        for (const auto& proto : *asset.dynamic_prototypes) {
            auto shape_result = CreateShape(proto.shape, combined_materials);
            if (!shape_result.shape) {
                result.error = "prototype '" + proto.proto_id + "': " + shape_result.error;
                return result;
            }

            auto layer_it = layer_config.object_layers.find(proto.object_layer);
            JPH::ObjectLayer obj_layer = (layer_it != layer_config.object_layers.end())
                ? JPH::ObjectLayer(layer_it->second) : JPH::ObjectLayer(1);

            PrototypeEntry entry;
            entry.proto_id = proto.proto_id;
            entry.shape = shape_result.shape;
            entry.mass = proto.mass;
            entry.friction = proto.material.friction;
            entry.restitution = proto.material.restitution;
            entry.motion_type = ParseMotionType(proto.motion_type);
            entry.motion_quality = ParseMotionQuality(proto.motion_quality);
            entry.linear_damping = proto.linear_damping;
            entry.angular_damping = proto.angular_damping;
            entry.gravity_factor = proto.gravity_factor;
            entry.object_layer = obj_layer;
            entry.is_sensor = proto.is_sensor;
            entry.allow_sleeping = proto.allow_sleeping;
            entry.max_linear_velocity = proto.max_linear_velocity;
            entry.max_angular_velocity = proto.max_angular_velocity;

            // Compute allowed DOFs bitmask
            entry.allowed_dofs = 0;
            for (uint8_t dof : proto.allowed_dofs) {
                if (dof < 6) entry.allowed_dofs |= (1 << dof);
            }

            prototypes_[entry.proto_id] = entry;
            ++result.dynamic_prototypes_loaded;
        }
    }

    // ── Load constraints ───────────────────────────────────────────────
    if (asset.constraints.has_value()) {
        for (const auto& con : *asset.constraints) {
            // Resolve bodyA and bodyB by asset name
            uint32_t body_a_id = 0;
            uint32_t body_b_id = 0;
            for (const auto& [id, name] : static_body_ids_) {
                if (name == con.body_a) body_a_id = id;
                if (name == con.body_b) body_b_id = id;
            }
            if (body_a_id == 0 || body_b_id == 0) {
                std::fprintf(stderr, "AssetLoader: constraint '%s' -> '%s': "
                            "referenced body not found\n",
                            con.body_a.c_str(), con.body_b.c_str());
                continue;  // skip broken constraints
            }

            JPH::RVec3 pivot = ParseVec3(con.pivot);
            JPH::Vec3 axis = JPH::Vec3(
                static_cast<float>(con.axis.size() > 0 ? con.axis[0] : 0.0),
                static_cast<float>(con.axis.size() > 1 ? con.axis[1] : 1.0),
                static_cast<float>(con.axis.size() > 2 ? con.axis[2] : 0.0));

            JPH::BodyID ja(body_a_id);
            JPH::BodyID jb(body_b_id);

            if (con.type == "hinge") {
                JPH::HingeConstraintSettings settings;
                settings.mPoint1 = settings.mPoint2 = pivot;
                settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
                settings.mLimitsMin = static_cast<float>(con.limits.min);
                settings.mLimitsMax = static_cast<float>(con.limits.max);
                JPH::TwoBodyConstraint* c = body_interface.CreateConstraint(&settings, ja, jb);
                if (c) {
                    physics_system.AddConstraint(c);
                    ++result.constraints_loaded;
                }
            }
            else if (con.type == "spring") {
                JPH::DistanceConstraintSettings settings;
                settings.mPoint1 = settings.mPoint2 = pivot;
                settings.mLimitsSpringSettings.mFrequency = con.spring.frequency;
                settings.mLimitsSpringSettings.mDamping = con.spring.damping;
                JPH::TwoBodyConstraint* c = body_interface.CreateConstraint(&settings, ja, jb);
                if (c) {
                    physics_system.AddConstraint(c);
                    ++result.constraints_loaded;
                }
            }
            else if (con.type == "slider") {
                JPH::SliderConstraintSettings settings;
                settings.mPoint1 = pivot;
                settings.mPoint2 = pivot;
                settings.mSliderAxis1 = axis;
                settings.mLimitsMin = static_cast<float>(con.limits.min);
                settings.mLimitsMax = static_cast<float>(con.limits.max);
                if (con.axis2.has_value()) {
                    settings.mSliderAxis2 = JPH::Vec3(
                        static_cast<float>((*con.axis2).size() > 0 ? (*con.axis2)[0] : 0.0),
                        static_cast<float>((*con.axis2).size() > 1 ? (*con.axis2)[1] : 0.0),
                        static_cast<float>((*con.axis2).size() > 2 ? (*con.axis2)[2] : 0.0));
                } else {
                    settings.mSliderAxis2 = axis;
                }
                JPH::TwoBodyConstraint* c = body_interface.CreateConstraint(&settings, ja, jb);
                if (c) {
                    physics_system.AddConstraint(c);
                    ++result.constraints_loaded;
                }
            }
            else if (con.type == "fixed") {
                JPH::FixedConstraintSettings settings;
                settings.mPoint1 = settings.mPoint2 = pivot;
                JPH::TwoBodyConstraint* c = body_interface.CreateConstraint(&settings, ja, jb);
                if (c) {
                    physics_system.AddConstraint(c);
                    ++result.constraints_loaded;
                }
            }
            else {
                std::fprintf(stderr, "AssetLoader: unknown constraint type '%s'. "
                            "Supported: hinge, spring, slider, fixed\n", con.type.c_str());
            }
        }
    }

    result.success = true;
    return result;
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
