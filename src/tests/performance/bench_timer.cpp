#include <benchmark/benchmark.h>
#include <runtime/core/timer/timer_manager.h>

// Timer creation throughput
static void BM_Timer_Create(benchmark::State& state) {
    engine::TimerManager tm;
    tm.initialize();

    for (auto _ : state) {
        auto id = tm.create_simple_timer([]() {});
        benchmark::DoNotOptimize(id);
        tm.destroy_timer(id);
    }

    tm.shutdown();
}
BENCHMARK(BM_Timer_Create);

// Timer update with N active timers
static void BM_Timer_Update(benchmark::State& state) {
    int count = static_cast<int>(state.range(0));
    engine::TimerManager tm;
    tm.initialize();

    for (int i = 0; i < count; ++i) {
        auto id = tm.create_simple_timer([]() {});
        tm.start_timer_relative(id, std::chrono::milliseconds(1000));
    }

    auto now = tm.now();
    for (auto _ : state) {
        now = now + std::chrono::milliseconds(1);
        auto result = tm.update(now);
        benchmark::DoNotOptimize(result.total_fired);
    }

    tm.shutdown();
}
BENCHMARK(BM_Timer_Update)->Arg(0)->Arg(100)->Arg(1000);
