#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

#ifdef ENGINE_PHYSICS_ENABLED
#include <runtime/physics/physics_engine_bridge.h>
#endif

using namespace std::chrono_literals;

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
	return bridge;
}
#endif
}

/* ============================================================================
 * Integration: PhysicsSystem::FetchResult CV-based waiting (P2-13)
 * ============================================================================ */

#ifdef ENGINE_PHYSICS_ENABLED

TEST_CASE("FetchResult returns within timeout when no data", "[integration][physics][cv]") {
	auto& bridge = StartFreshPhysics();

	// Tick to produce a frame
	REQUIRE(bridge.Tick(1, 0.016f));

	auto start = std::chrono::steady_clock::now();
	auto result = bridge.FetchResult(1, 100);
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();

	// Should return quickly — not spin for full 100ms
	REQUIRE(elapsed < 150);
	bridge.Shutdown();
}

TEST_CASE("FetchResult timeout returns nullopt", "[integration][physics][cv]") {
	auto& bridge = StartFreshPhysics();

	// Request a frame_id that was never produced — should time out
	auto start = std::chrono::steady_clock::now();
	auto result = bridge.FetchResult(999999, 50);
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();

	REQUIRE_FALSE(result.has_value());
	// Should wait ~50ms, not spin (allow some margin for CV signaling overhead)
	REQUIRE(elapsed >= 40);
	REQUIRE(elapsed < 100);
	bridge.Shutdown();
}

TEST_CASE("FetchResult receives result from physics thread without busy-wait", "[integration][physics][cv]") {
	auto& bridge = StartFreshPhysics();

	uint64_t frame_id = 100;
	REQUIRE(bridge.Tick(frame_id, 0.016f));

	auto start = std::chrono::steady_clock::now();
	auto result = bridge.FetchResult(frame_id, 200);
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - start).count();

	REQUIRE(result.has_value());
	REQUIRE(result->frame_id == frame_id);
	// With CV notification, result should arrive quickly (< 5ms once available)
	REQUIRE(elapsed < 5000);
	bridge.Shutdown();
}

TEST_CASE("Multiple FetchResult calls without new data wait on CV", "[integration][physics][cv]") {
	auto& bridge = StartFreshPhysics();

	// First fetch returns data
	uint64_t frame_id = 200;
	REQUIRE(bridge.Tick(frame_id, 0.016f));
	auto r1 = bridge.FetchResult(frame_id, 100);
	REQUIRE(r1.has_value());

	// Second fetch with no new tick — should time out via CV wait, not spin
	auto start = std::chrono::steady_clock::now();
	auto r2 = bridge.FetchResult(frame_id + 1, 30);
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();

	REQUIRE_FALSE(r2.has_value());
	// Should have waited ~30ms on CV (not returned instantly via spin)
	REQUIRE(elapsed >= 25);
	bridge.Shutdown();
}

#endif  // ENGINE_PHYSICS_ENABLED
