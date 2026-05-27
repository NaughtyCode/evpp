// Lua test runner — creates a minimal Engine in library mode,
// executes one or more Lua test files using absolute paths.
//
// Usage: lua_test_runner <test_dir> <test1.lua> [test2.lua ...]
//
// The test_dir is prepended to Lua's package.path so that require()
// can find harness modules relative to the tests directory.

#include "wsa_init.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <runtime/engine/engine.h>
#include <runtime/config/config.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/vm/vm.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: lua_test_runner <test_file.lua> [more_test_files...]\n";
        return 1;
    }

    // ── Minimal config ──────────────────────────────────────────────
    auto& cfg = engine::ConfigManager::Instance();
    if (!cfg.LoadRuntimeFromString(R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "error" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "resources/script/runtime"
    })")) {
        std::cerr << "Failed to load runtime config\n";
        return 1;
    }

    // ── Create engine in library mode ───────────────────────────────
    auto& engine = engine::Engine::Instance();
    evpp::EventLoop loop;
    auto rt_cfg = cfg.GetRuntimeConfig();
    engine.Init(rt_cfg, "resources/script/server", &loop);

    auto& vm = engine.GetScriptVM();

    // ── Run each test file via absolute path ────────────────────────
    for (int i = 1; i < argc; ++i) {
        std::string filepath(argv[i]);
        std::string err;
        if (!vm.DoFile(filepath, &err)) {
            std::cerr << "FAIL: " << filepath << " — " << err << "\n";
            engine.Cleanup();
            return 1;
        }
    }

    engine.Cleanup();
    return 0;
}
