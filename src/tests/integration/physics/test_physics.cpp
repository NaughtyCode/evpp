#include <catch2/catch_test_macros.hpp>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

#ifdef ENGINE_PHYSICS_ENABLED
#include <runtime/physics/physics_engine_bridge.h>
#endif

namespace {
constexpr const char* kPhysicsConfigDir = "resources/physics/config";
constexpr const char* kPhysicsScriptsDir = "";

#ifdef ENGINE_PHYSICS_ENABLED
engine::PhysicsEngineBridge& StartFreshPhysics() {
	auto& bridge = engine::PhysicsEngineBridge::Instance();
	if (bridge.IsInitialized()) {
		bridge.Shutdown();
	}
	REQUIRE(bridge.Initialize(kPhysicsConfigDir, kPhysicsScriptsDir));
	REQUIRE(bridge.Start());
	REQUIRE(bridge.IsRunning());
	REQUIRE(bridge.IsHealthy());
	return bridge;
}
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration: Physics engine bridge lifecycle
// ═══════════════════════════════════════════════════════════════════════════
// These tests are only compiled when ENGINE_PHYSICS_ENABLED is ON.

#ifdef ENGINE_PHYSICS_ENABLED

TEST_CASE("Physics bridge initialize and shutdown", "[integration][physics]") {
    auto& bridge = StartFreshPhysics();

    // Shutdown
    bridge.Shutdown();
    REQUIRE_FALSE(bridge.IsRunning());
}

TEST_CASE("Physics bridge Tick completes without error", "[integration][physics]") {
    auto& bridge = StartFreshPhysics();

    REQUIRE(bridge.Tick(1, 0.016f));

    bridge.Shutdown();
}

TEST_CASE("Physics bridge command helpers spawn query and destroy body", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();

	REQUIRE(bridge.EnqueueSpawn("crate", 0.0, 2.0, 0.0));

	uint64_t frame_id = 1000;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	auto result = bridge.FetchResult(frame_id, 250);
	REQUIRE(result.has_value());
	REQUIRE(result->error.empty());
	REQUIRE_FALSE(result->transforms.empty());

	uint32_t body_id = result->transforms.front().body_id;
	REQUIRE(bridge.GetTransform(body_id).has_value());
	REQUIRE(bridge.GetVelocity(body_id).has_value());

	auto stats = bridge.GetPhysicsStats();
	REQUIRE(stats.total_bodies >= 1);

	REQUIRE(bridge.EnqueueSetVelocity(body_id, 1.0f, 0.0f, 0.0f));
	REQUIRE(bridge.EnqueueApplyForce(body_id, 0.0f, 10.0f, 0.0f, 0.0, 2.0, 0.0));

	++frame_id;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	REQUIRE(bridge.FetchResult(frame_id, 250).has_value());

	REQUIRE(bridge.EnqueueDestroy(body_id));
	++frame_id;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	REQUIRE(bridge.FetchResult(frame_id, 250).has_value());
	REQUIRE_FALSE(bridge.GetTransform(body_id).has_value());

	bridge.Shutdown();
}

TEST_CASE("Physics bridge rejects invalid tick input", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();
	REQUIRE_FALSE(bridge.Tick(42, 0.0f));
	bridge.Shutdown();
}

TEST_CASE("Physics bridge recover restarts a healthy physics thread", "[integration][physics]") {
	auto& bridge = StartFreshPhysics();
	REQUIRE(bridge.Recover());
	REQUIRE(bridge.IsRunning());
	REQUIRE(bridge.IsHealthy());
	bridge.Shutdown();
}

#endif  // ENGINE_PHYSICS_ENABLED

// Fallback when physics is disabled
#ifndef ENGINE_PHYSICS_ENABLED
TEST_CASE("Physics integration tests skipped (engine built without physics)", "[integration][physics]") {
    SUCCEED("Physics is disabled — skipping integration tests");
}
#endif
