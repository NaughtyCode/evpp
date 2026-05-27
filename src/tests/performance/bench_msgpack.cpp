#include <benchmark/benchmark.h>
#include <runtime/vm/vm.h>
#include <string>

// MsgPack encode/decode roundtrip via ScriptVM
// Tests the C++ side of msgpack (not Lua-side pack/unpack).
static void BM_MsgPack_DoString(benchmark::State& state) {
    std::string code = "return " + std::to_string(state.range(0));
    engine::ScriptVM vm;

    for (auto _ : state) {
        std::string result;
        vm.DoString(code, "bench", nullptr, &result);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_MsgPack_DoString)->Arg(1)->Arg(1000);

// ScriptVM Lua execution overhead
static void BM_ScriptVM_Empty(benchmark::State& state) {
    engine::ScriptVM vm;

    for (auto _ : state) {
        vm.DoString("return 42", "bench");
    }
}
BENCHMARK(BM_ScriptVM_Empty);
