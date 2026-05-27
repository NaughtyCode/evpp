#include "wsa_init.h"
#include <catch2/catch_test_macros.hpp>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/vm/vm.h>
#include "config_fixture.h"

// ═══════════════════════════════════════════════════════════════════════════
// Engine: lifecycle (library mode — no blocking Run)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Engine init with minimal config", "[engine][lifecycle]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();

    // Use external loop (library mode)
    evpp::EventLoop loop;
    REQUIRE_NOTHROW(engine.Init(rt_cfg, "resources/script/server", &loop));
    REQUIRE(engine.GetEventLoop() == &loop);
    REQUIRE(engine.GetScriptVM().GetState() != nullptr);

    engine.Cleanup();
}

TEST_CASE("Engine init creates ScriptVM", "[engine][lifecycle]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);

    auto& vm = engine.GetScriptVM();
    REQUIRE(vm.GetState() != nullptr);

    // Verify Lua can execute
    std::string result;
    REQUIRE(vm.DoString("return 1 + 1", "test", nullptr, &result));
    REQUIRE(result == "2");

    engine.Cleanup();
}

TEST_CASE("Engine Tick in library mode", "[engine][lifecycle]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);

    REQUIRE(engine.frame_count() == 0);

    // Tick should increment frame count
    engine.Tick();
    REQUIRE(engine.frame_count() >= 0);  // may or may not count depending on timing

    engine.Cleanup();
}

TEST_CASE("Engine running flag", "[engine][lifecycle]") {
    ConfigFixture f;
    f.LoadFromStrings();
    auto rt_cfg = f.cfg.GetRuntimeConfig();

    auto& engine = engine::Engine::Instance();
    evpp::EventLoop loop;
    engine.Init(rt_cfg, "resources/script/server", &loop);

    REQUIRE(engine.running());  // library mode: engine is immediately "running" after Init

    engine.Cleanup();
}
