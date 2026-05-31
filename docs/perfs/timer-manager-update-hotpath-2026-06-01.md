# TimerManager Update Hot Path Optimization

Date: 2026-06-01

## Scope

Reviewed `src/runtime` from the perspective of game-server tick-loop cost. This round focused on `src/runtime/core/timer`, because `TimerManager::update()` is a per-frame/per-loop hot path and is already covered by `bench_timer`.

## Finding

`TimerManager::update_hrtimers()` and `TimerManager::update_wheel()` refreshed subsystem statistics after every update:

- `hrtimer_mgr_->stats()`
- `wheel_->stats()`

Those statistics are observability data. Public reads go through `TimerManager::stats()`, which already samples `hrtimer_mgr_` and `wheel_` on demand before returning. Keeping the refresh in every update added avoidable lock/copy work to the tick path without changing timer firing behavior.

## Change

Removed the per-update subsystem stats refresh from:

- `src/runtime/core/timer/timer_manager.cc`
  - `TimerManager::update_hrtimers`
  - `TimerManager::update_wheel`

Added a regression test in `src/tests/unit/timer/test_timer.cpp` to verify `TimerManager::stats()` still reports fresh HR timer and wheel expiry counters after timers fire.

## Correctness

Before:

```text
artifacts\bin\Release\test_timer.exe
All tests passed (33 assertions in 12 test cases)
```

After:

```text
artifacts\bin\Release\test_timer.exe
All tests passed (39 assertions in 13 test cases)

ctest --test-dir artifacts\build-perf-timer -C Release -R "^(unit\.timer|performance\.bench_timer)$" --output-on-failure
100% tests passed, 0 tests failed out of 2
```

Note: `ctest -R "timer"` also selects two Lua timer tests in this checkout. They failed before final gating because the local Lua import path cannot find `tests.harness.test_harness`; `unit.timer` and `performance.bench_timer` passed in that same run.

## Performance

Command used before and after:

```text
artifacts\bin\Release\bench_timer.exe --benchmark_min_time=0.2s --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_format=console
```

Host reported by Google Benchmark:

```text
32 X 4292 MHz CPU
L1 Data 48 KiB x16, L1 Instruction 32 KiB x16, L2 1024 KiB x16, L3 98304 KiB x2
```

Mean wall-time results:

| Benchmark | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `BM_Timer_Create_mean` | 153 ns | 156 ns | -2.0% |
| `BM_Timer_Update/0_mean` | 323 ns | 308 ns | +4.6% |
| `BM_Timer_Update/100_mean` | 340 ns | 323 ns | +5.0% |
| `BM_Timer_Update/1000_mean` | 340 ns | 324 ns | +4.7% |

Mean CPU-time results:

| Benchmark | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `BM_Timer_Create_mean` | 153 ns | 157 ns | -2.6% |
| `BM_Timer_Update/0_mean` | 324 ns | 307 ns | +5.2% |
| `BM_Timer_Update/100_mean` | 342 ns | 324 ns | +5.3% |
| `BM_Timer_Update/1000_mean` | 338 ns | 321 ns | +5.0% |

`BM_Timer_Create` is not on the modified path, so the small regression is treated as benchmark noise. The targeted `TimerManager::update()` path improved by about 4.6% to 5.3% across tested active-timer counts.
