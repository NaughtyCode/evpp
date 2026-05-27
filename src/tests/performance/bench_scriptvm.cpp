#include <benchmark/benchmark.h>
#include <runtime/vm/vm.h>
#include <string>

// ScriptVM DoString throughput with simple expression
static void BM_ScriptVM_DoString(benchmark::State& state) {
    engine::ScriptVM vm;
    std::string result;

    for (auto _ : state) {
        vm.DoString("return 1 + 2 * 3", "bench", nullptr, &result);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_ScriptVM_DoString);

// ScriptVM RegisterFunction + call from Lua
static int add_two(lua_State* L) {
    lua_pushinteger(L, 2);
    return 1;
}

static void BM_ScriptVM_CFunctionCall(benchmark::State& state) {
    engine::ScriptVM vm;
    vm.RegisterFunction("add_two", add_two);

    for (auto _ : state) {
        vm.DoString("add_two()", "bench");
    }
}
BENCHMARK(BM_ScriptVM_CFunctionCall);
