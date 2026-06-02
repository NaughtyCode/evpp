#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

#ifdef ENGINE_PHYSICS_ENABLED
#include <runtime/physics/physics_engine_bridge.h>
#endif

namespace {
constexpr const char* kPhysicsConfigDir = "resources/physics/config";
constexpr const char* kFullPhysicsConfigDir = "resources/physics_full/config";
constexpr const char* kPhysicsScriptsDir = "";

#ifdef ENGINE_PHYSICS_ENABLED
constexpr const char* kFullPhysicsConfigJson = R"json({
  "version": 1,
  "scenePath": "/physics/data/full_scene.json",
  "gravityX": 0.0,
  "gravityY": -9.81,
  "gravityZ": 0.0,
  "fixedDeltaTime": 0.01667,
  "solverIterations": 10,
  "subStepCount": 2,
  "maxBodies": 4096,
  "maxContactPoints": 10240,
  "maxBodyPairs": 16384,
  "numBodyMutexes": 32,
  "layerConfig": {
    "objectLayers": {
      "static": 0,
      "dynamic": 1,
      "trigger": 2
    },
    "broadPhaseLayers": {
      "static": 0,
      "dynamic": 1
    },
    "layerMapping": {
      "static": "static",
      "dynamic": "dynamic",
      "trigger": "dynamic"
    },
    "collisionMatrix": [
      {"layerA": "static", "layerB": "static", "collide": false},
      {"layerA": "static", "layerB": "dynamic", "collide": true},
      {"layerA": "static", "layerB": "trigger", "collide": true},
      {"layerA": "dynamic", "layerB": "dynamic", "collide": true},
      {"layerA": "dynamic", "layerB": "trigger", "collide": true},
      {"layerA": "trigger", "layerB": "trigger", "collide": false}
    ]
  }
})json";

std::string PrepareFullPhysicsConfig() {
	const std::filesystem::path full_config_dir(kFullPhysicsConfigDir);
	std::error_code ec;
	std::filesystem::create_directories(full_config_dir, ec);
	REQUIRE_FALSE(ec);

	auto copy_config = [&](const char* file_name) {
		ec.clear();
		std::filesystem::copy_file(std::filesystem::path(kPhysicsConfigDir) / file_name,
								   full_config_dir / file_name,
								   std::filesystem::copy_options::overwrite_existing,
								   ec);
		REQUIRE_FALSE(ec);
	};
	copy_config("threading.json");
	copy_config("logging.json");
	copy_config("thresholds.json");

	std::ofstream physics_config(full_config_dir / "physics.json",
								 std::ios::binary | std::ios::trunc);
	REQUIRE(physics_config.good());
	physics_config << kFullPhysicsConfigJson;
	REQUIRE(physics_config.good());

	return full_config_dir.string();
}

engine::PhysicsEngineBridge& StartFreshPhysics(const std::string& config_dir = kPhysicsConfigDir) {
	auto& bridge = engine::PhysicsEngineBridge::Instance();
	if (bridge.IsInitialized()) {
		bridge.Shutdown();
	}
	REQUIRE(bridge.Initialize(config_dir, kPhysicsScriptsDir));
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

TEST_CASE("Physics bridge loads full resource data and simulates spawned prototypes",
		  "[integration][physics][data]") {
	const std::string full_config_dir = PrepareFullPhysicsConfig();
	auto& bridge = StartFreshPhysics(full_config_dir);

	const auto initial_stats = bridge.GetPhysicsStats();
	constexpr uint32_t kExpectedSceneBodies = 8;
	REQUIRE(initial_stats.total_bodies >= kExpectedSceneBodies);

	auto ground_hit = bridge.RayCast(0.0, 8.0, 0.0, 0.0, -1.0, 0.0, 20.0f);
	REQUIRE(ground_hit.has_value());

	struct SpawnCase {
		const char* proto_id;
		double x;
		double y;
		double z;
	};
	const std::vector<SpawnCase> spawn_cases = {
		{"crate_full", -5.0, 4.0, 3.0},
		{"ball_full", -3.0, 4.0, 3.0},
		{"capsule_full", -1.0, 4.0, 3.0},
		{"cylinder_full", 1.0, 4.0, 3.0},
		{"hull_full", 3.0, 4.0, 3.0},
		{"compound_full", 5.0, 4.0, 3.0},
	};

	for (const auto& spawn : spawn_cases) {
		REQUIRE(bridge.EnqueueSpawn(spawn.proto_id, spawn.x, spawn.y, spawn.z));
	}

	constexpr uint64_t kSpawnFrame = 5000;
	REQUIRE(bridge.Tick(kSpawnFrame, bridge.GetFixedDeltaTime()));
	auto spawn_result = bridge.FetchResult(kSpawnFrame, 500);
	REQUIRE(spawn_result.has_value());
	INFO(spawn_result->error);
	REQUIRE(spawn_result->error.empty());
	REQUIRE(spawn_result->frame_id == kSpawnFrame);
	REQUIRE(spawn_result->transforms.size() >= spawn_cases.size());

	const auto after_spawn_stats = bridge.GetPhysicsStats();
	REQUIRE(after_spawn_stats.total_bodies >=
			initial_stats.total_bodies + static_cast<uint32_t>(spawn_cases.size()));

	const uint32_t body_id = spawn_result->transforms.front().body_id;
	REQUIRE(bridge.GetTransform(body_id).has_value());
	REQUIRE(bridge.GetVelocity(body_id).has_value());
	REQUIRE(bridge.EnqueueSetVelocity(body_id, 0.25f, 0.0f, 0.0f));
	REQUIRE(bridge.EnqueueApplyForce(body_id,
									 0.0f,
									 8.0f,
									 0.0f,
									 spawn_result->transforms.front().pos_x,
									 spawn_result->transforms.front().pos_y,
									 spawn_result->transforms.front().pos_z));

	constexpr uint64_t kForceFrame = kSpawnFrame + 1;
	REQUIRE(bridge.Tick(kForceFrame, bridge.GetFixedDeltaTime()));
	auto force_result = bridge.FetchResult(kForceFrame, 500);
	REQUIRE(force_result.has_value());
	INFO(force_result->error);
	REQUIRE(force_result->error.empty());

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
