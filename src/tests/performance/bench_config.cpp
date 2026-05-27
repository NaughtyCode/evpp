#include <benchmark/benchmark.h>
#include <runtime/config/config.h>

// ConfigManager JSON parsing throughput
static void BM_Config_ParseRuntime(benchmark::State& state) {
    std::string json = R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "info" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "resources/script/runtime"
    })";

    for (auto _ : state) {
        engine::ConfigManager::Instance().LoadRuntimeFromString(json);
    }
}
BENCHMARK(BM_Config_ParseRuntime);

// ConfigManager read access (thread-safe path)
static void BM_Config_GetRuntime(benchmark::State& state) {
    engine::ConfigManager::Instance().LoadRuntimeFromString(R"({
        "resource_dir": "resources",
        "log": { "dir": "logs", "level": "info" },
        "frame": { "target_fps": 30, "interval_ms": 33 },
        "scripts_dir": "resources/script/runtime"
    })");

    for (auto _ : state) {
        auto cfg = engine::ConfigManager::Instance().GetRuntimeConfig();
        benchmark::DoNotOptimize(cfg.frame.target_fps);
    }
}
BENCHMARK(BM_Config_GetRuntime);
