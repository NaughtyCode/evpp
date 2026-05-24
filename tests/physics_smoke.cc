// Minimal smoke test for the physics subsystem.
// Links against engine_physics.lib and Jolt.lib directly.
// Tests: MaterialTable, LayerConfig, AssetLoader shape creation.

#include <cstdio>
#include <vector>
#include <string>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include "engine/physics/physics_config.h"
#include "engine/physics/physics_materials.h"
#include "engine/physics/physics_layers.h"
#include "engine/physics/physics_assets.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s\n", msg); \
        ++failures; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
    } \
} while(0)

int main() {
    std::printf("=== Physics Subsystem Smoke Test ===\n\n");

    // ═══════════════════════════════════════════════════════════════
    // Test 1: Jolt Registration
    // ═══════════════════════════════════════════════════════════════
    std::printf("--- 1. Jolt Registration ---\n");
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    CHECK(true, "JoltPhysics registered");

    // ═══════════════════════════════════════════════════════════════
    // Test 2: MaterialTable
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n--- 2. MaterialTable ---\n");
    engine::MaterialTable mats;
    const char* json = R"([
        {"name": "ice", "friction": 0.05, "restitution": 0.1},
        {"name": "rubber", "friction": 1.0, "restitution": 0.9}
    ])";
    CHECK(mats.LoadFromJson(json), "LoadFromJson");
    CHECK(mats.Size() == 2, "Size == 2");
    CHECK(!mats.Empty(), "!Empty()");

    auto ice = mats.Get("ice");
    CHECK(ice != JPH::PhysicsMaterial::sDefault, "Get('ice') found");
    CHECK(mats.Get("nonexistent") == JPH::PhysicsMaterial::sDefault, "Get('nonexistent') returns default");

    auto names = mats.GetNames();
    CHECK(names.size() == 2, "GetNames returns 2");

    // CreateList
    auto list = mats.CreateList({"ice", "rubber"});
    CHECK(list.size() == 2, "CreateList size 2");

    // Clear
    mats.Clear();
    CHECK(mats.Empty(), "Clear + Empty");

    // ═══════════════════════════════════════════════════════════════
    // Test 3: LayerConfig validation
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n--- 3. LayerConfig ---\n");
    engine::LayerConfig lc;
    lc.object_layers["static"] = 0;
    lc.object_layers["dynamic"] = 1;
    lc.broad_phase_layers["non_moving"] = 0;
    lc.broad_phase_layers["moving"] = 1;
    lc.layer_mapping["static"] = "non_moving";
    lc.layer_mapping["dynamic"] = "moving";
    lc.collision_matrix.push_back({"static", "dynamic", true});
    lc.collision_matrix.push_back({"dynamic", "dynamic", true});

    engine::BPLayerInterfaceImpl bp_iface(lc);
    CHECK(bp_iface.GetNumBroadPhaseLayers() == 2, "BP layers == 2");

    engine::ObjectLayerPairFilterImpl pair_filter(lc);
    CHECK(pair_filter.ShouldCollide(JPH::ObjectLayer(0), JPH::ObjectLayer(1)),
          "static-dynamic should collide");

    engine::ObjectVSBLayerFilterImpl vs_bp_filter(lc, bp_iface);
    CHECK(vs_bp_filter.ShouldCollide(JPH::ObjectLayer(1),
          bp_iface.GetBroadPhaseLayer(JPH::ObjectLayer(0))),
          "object-vs-bp filter collision");

    // ═══════════════════════════════════════════════════════════════
    // Test 4: AssetLoader — shape creation
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n--- 4. AssetLoader shape creation ---\n");
    engine::MaterialTable mt;
    mt.LoadFromJson(json);  // reload

    engine::AssetLoader loader;
    engine::LayerConfig dummy_lc;
    dummy_lc.object_layers["default"] = 0;
    dummy_lc.broad_phase_layers["default"] = 0;
    dummy_lc.layer_mapping["default"] = "default";

    // Box
    {
        engine::JsonShapeDef def;
        def.type = "box";
        def.params = glz::generic{};
        auto result = loader.CreateShape(def, mt);
        CHECK(result.shape != nullptr && result.error.empty(),
              "box shape created");
    }
    // Sphere
    {
        engine::JsonShapeDef def;
        def.type = "sphere";
        def.params = glz::generic{};
        auto result = loader.CreateShape(def, mt);
        CHECK(result.shape != nullptr && result.error.empty(),
              "sphere shape created");
    }
    // Capsule
    {
        engine::JsonShapeDef def;
        def.type = "capsule";
        def.params = glz::generic{};
        auto result = loader.CreateShape(def, mt);
        CHECK(result.shape != nullptr && result.error.empty(),
              "capsule shape created");
    }
    // Plane
    {
        engine::JsonShapeDef def;
        def.type = "plane";
        def.params = glz::generic{};
        auto result = loader.CreateShape(def, mt);
        CHECK(result.shape != nullptr && result.error.empty(),
              "plane shape created");
    }
    // Unknown type
    {
        engine::JsonShapeDef def;
        def.type = "unknown_shape";
        def.params = glz::generic{};
        auto result = loader.CreateShape(def, mt);
        CHECK(result.shape == nullptr && !result.error.empty(),
              "unknown shape returns error");
    }

    // ═══════════════════════════════════════════════════════════════
    // Summary
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n=== Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
