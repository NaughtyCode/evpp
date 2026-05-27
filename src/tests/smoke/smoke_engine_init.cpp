#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>

// Smoke test: engine Init → Start → Shutdown → Cleanup cycle.
// Verifies no crashes or deadlocks in the most basic lifecycle.

TEST_CASE("Engine full init/shutdown cycle", "[smoke][engine]") {
    auto& cfg = engine::ConfigManager::Instance();

    std::string runtime_json = R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "info" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "resources/script/runtime"
    })";
    REQUIRE(cfg.LoadRuntimeFromString(runtime_json));

    std::string server_json = R"({
        "http": { "timeout_sec": 5.0 },
        "msgpack": { "max_nesting_depth": 16 },
        "scripts_dir": "resources/script/server"
    })";
    REQUIRE(cfg.LoadServerFromString(server_json));

    auto& engine = engine::Engine::Instance();
    auto rt_cfg = cfg.GetRuntimeConfig();

    REQUIRE_NOTHROW(engine.Init(rt_cfg, "resources/script/server"));
    REQUIRE(engine.GetEventLoop() != nullptr);

    engine.Start();
    REQUIRE(engine.running());

    // Shutdown after a brief pause — schedule via the event loop
    engine.GetEventLoop()->RunAfter(0.1, [&engine]() {
        engine.Shutdown();
    });

    // Run blocks until Shutdown
    engine.Run();
    // Cleanup is called automatically after Run() in standalone mode
    REQUIRE_FALSE(engine.running());
}
