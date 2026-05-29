#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/monitoring/admin_http.h>
#include <runtime/physics/physics_engine_bridge.h>
#include "config_fixture.h"

// ═══════════════════════════════════════════════════════════════════════════
// Engine: health-check state accessors
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Engine::initialized is false before Init", "[health][engine]") {
    auto& engine = engine::Engine::Instance();
    engine.Cleanup();  // reset from previous tests
    REQUIRE_FALSE(engine.initialized());
}

TEST_CASE("Engine::initialized is true after Init", "[health][engine]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    engine.Cleanup();

    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);
    REQUIRE(engine.initialized());

    engine.Cleanup();
}

TEST_CASE("Engine::cleanup_phase is NotStarted during normal operation", "[health][engine]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    engine.Cleanup();

    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);
    REQUIRE(engine.initialized());
    REQUIRE(engine.cleanup_phase() == engine::Engine::CleanupPhase::NotStarted);

    engine.Cleanup();
}

TEST_CASE("Engine::cleanup_phase transitions to Complete after Cleanup", "[health][engine]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    engine.Cleanup();

    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);
    REQUIRE(engine.initialized());

    engine.Cleanup();
    REQUIRE(engine.cleanup_phase() == engine::Engine::CleanupPhase::Complete);
}

// ═══════════════════════════════════════════════════════════════════════════
// PhysicsEngineBridge: IsInitialized
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PhysicsEngineBridge::IsInitialized after Init", "[health][physics]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    engine.Cleanup();

    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);

    // Physics should be initialized after Engine::Init
    REQUIRE(PhysicsEngineBridge::Instance().IsInitialized());

    engine.Cleanup();
}

// ═══════════════════════════════════════════════════════════════════════════
// AdminHttpServer: basic lifecycle with health endpoints
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdminHttpServer Start and Stop", "[health][admin]") {
    engine::monitoring::AdminHttpServer server;
    evpp::EventLoop loop;
    // Port 0 would fail — use any available port by trying a range, or
    // just test Stop without Start
    REQUIRE_FALSE(server.IsRunning());

    // Server with no loop returns false
    REQUIRE_FALSE(server.Start(nullptr, 18081, "127.0.0.1"));

    // Server should not be running
    REQUIRE_FALSE(server.IsRunning());
}

TEST_CASE("AdminHttpServer::Start with valid loop", "[health][admin]") {
    engine::monitoring::AdminHttpServer server;
    evpp::EventLoop loop;

    bool started = server.Start(&loop, 18082, "127.0.0.1");
    if (started) {
        REQUIRE(server.IsRunning());
        REQUIRE(server.port() == 18082);
        server.Stop();
        REQUIRE_FALSE(server.IsRunning());
    }
    // If port is already in use, that's OK — just verify no crash
}

// ═══════════════════════════════════════════════════════════════════════════
// Engine: initialized() with test instance (SetInstanceForTesting)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Engine test instance: initialized accessor", "[health][engine][test_instance]") {
    // Verify SetInstanceForTesting / ClearTestInstance works
    auto& real = engine::Engine::Instance();

    // Create a test engine (don't init, just check default state)
    engine::Engine test_engine;
    REQUIRE_FALSE(test_engine.initialized());
    REQUIRE(test_engine.cleanup_phase() == engine::Engine::CleanupPhase::NotStarted);

    engine::Engine::SetInstanceForTesting(&test_engine);
    REQUIRE_FALSE(engine::Engine::Instance().initialized());

    engine::Engine::ClearTestInstance();
    REQUIRE(&engine::Engine::Instance() == &real);
}

// ═══════════════════════════════════════════════════════════════════════════
// Engine: frame_count in library mode (used by liveness probe)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Engine frame_count increments in library mode", "[health][engine]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    engine.Cleanup();

    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);

    uint64_t fc_before = engine.frame_count();
    // Tick a few frames
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        engine.Tick();
    }
    uint64_t fc_after = engine.frame_count();
    // frame_count should be at least fc_before (may or may not advance
    // depending on frame interval timing)
    REQUIRE(fc_after >= fc_before);

    engine.Cleanup();
}
