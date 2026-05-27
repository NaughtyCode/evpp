#include <catch2/catch_test_macros.hpp>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

#ifdef ENGINE_PHYSICS_ENABLED
#include <runtime/physics/physics_engine_bridge.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Integration: Physics engine bridge lifecycle
// ═══════════════════════════════════════════════════════════════════════════
// These tests are only compiled when ENGINE_PHYSICS_ENABLED is ON.

#ifdef ENGINE_PHYSICS_ENABLED

TEST_CASE("Physics bridge initialize and shutdown", "[integration][physics]") {
    auto& bridge = engine::PhysicsEngineBridge::Instance();

    // Initialize with a simple config
    REQUIRE_NOTHROW(bridge.Initialize("resources/config"));

    REQUIRE(bridge.IsInitialized());

    // Start the physics thread
    bridge.Start();
    REQUIRE(bridge.IsRunning());
    REQUIRE(bridge.IsHealthy());

    // Shutdown
    bridge.Shutdown();
    REQUIRE_FALSE(bridge.IsRunning());
}

TEST_CASE("Physics bridge Tick completes without error", "[integration][physics]") {
    auto& bridge = engine::PhysicsEngineBridge::Instance();

    if (!bridge.IsInitialized()) {
        bridge.Initialize("resources/config");
    }
    if (!bridge.IsRunning()) {
        bridge.Start();
    }

    // Should not throw
    REQUIRE_NOTHROW(bridge.Tick(1, 0.016f));

    bridge.Shutdown();
}

#endif  // ENGINE_PHYSICS_ENABLED

// Fallback when physics is disabled
#ifndef ENGINE_PHYSICS_ENABLED
TEST_CASE("Physics integration tests skipped (engine built without physics)", "[integration][physics]") {
    SUCCEED("Physics is disabled — skipping integration tests");
}
#endif
