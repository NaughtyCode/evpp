# P0-3: Test Infrastructure — Framework, Fakes, Static Analysis & Fuzz Targets

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p0/p0-3-test-infrastructure.md`

## Summary

Established the remaining test infrastructure gaps after the individual P0 plans
(P0-2 through P0-1) had already built Catch2 v3 integration, CI/CD pipelines, 12
unit test files, 4 integration tests, 11 Lua test files, 6 performance benchmarks,
5 smoke tests, and 8 test fixtures alongside their respective implementations.

This plan focused on the missing pieces: fake library for test isolation, static
analysis configuration, Engine singleton decoupling for testability, fuzz targets
for security-critical input boundaries, and C++ test utilities.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/tests/fakes/fake_event_loop.h` | Synchronous EventLoop — queues RunInLoop/QueueInLoop callbacks, Pump() to drain, virtual-time RunAfter/RunEvery |
| `src/tests/fakes/fake_timer_manager.h` | Virtual-time TimerManager — create_simple_timer, create_repeating_simple_timer, create_timer_for, AdvanceTime() fires due timers |
| `src/tests/fakes/fake_tcp_connection.h` | Fake TCP connection — buffers Send() data, manual DeliverMessage(), controllable connect/disconnect |
| `src/tests/fakes/fake_network_clock.h` | Controllable clock — time starts at zero, only advances when explicitly told |
| `src/tests/fakes/fake_random_source.h` | Deterministic LCG RNG — seedable for reproducible test runs |
| `src/tests/test_utils/scope_guard.h` | RAII `ScopeGuard` — runs cleanup callback on destruction unless dismissed |
| `src/tests/test_utils/async_helper.h` | `WaitFor(predicate, timeout)` and `WaitForAction(predicate, action, timeout)` polling helpers |
| `src/tests/test_utils/lua_assertions.h` | `ExpectLuaStackSize`, `ExpectLuaStackClean`, `ExpectLuaGlobalString/Int/Nil/Table` for C++ Lua state inspection |
| `.clang-tidy` | Static analysis config — bugprone, performance, modernize, cppcoreguidelines, readability checks with project-specific exclusions |
| `resources/.luacheckrc` | Lua lint config — engine API globals, test harness globals, file exclusions, per-directory overrides |
| `src/tests/fuzz/network_frame_fuzz.cpp` | Fuzz target for `LengthPrefixedCodec::Decode` — feeds arbitrary bytes to the frame decoder |
| `src/tests/fuzz/config_parse_fuzz.cpp` | Fuzz target for `ConfigManager::LoadRuntimeFromString` — feeds arbitrary bytes to JSON config parser |
| `src/tests/fuzz/msgpack_decode_fuzz.cpp` | Fuzz target for Lua msgpack.decode — feeds arbitrary bytes through sandboxed Lua VM |
| `src/tests/fuzz/lua_dostring_fuzz.cpp` | Fuzz target for sandboxed Lua DoString — feeds arbitrary strings through all 3 sandbox levels |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/engine/engine.h` | Added `SetInstanceForTesting(Engine*)` and `ClearTestInstance()` static methods |
| `src/runtime/engine/engine.cc` | Implemented test instance injection — `Instance()` returns test instance when set, falls back to global singleton otherwise |

## Design Decisions

1. **Catch2 over GoogleTest**: The plan originally recommended GoogleTest, but all existing
   tests (P0-2 through P0-1) already use Catch2 v3 (276+ assertions). Migrating would be
   pure churn with no benefit. The existing Catch2 infrastructure is production-quality.

2. **Header-only fakes**: All fakes are single-header files with no .cc counterparts.
   This minimizes build complexity and makes them trivial to include in new test files.
   Each fake is ~50-120 lines.

3. **Fakes for existing interfaces only**: The plan called for 8 fakes, but
   `FakeDatabaseService`, `FakePhysicsSystem`, and `FakeConfigManager` were skipped
   because those subsystems don't have stable abstract interfaces yet. Creating fakes
   for unstable interfaces leads to fake drift. These will be added when the real
   interfaces stabilize (P1/P2).

4. **Engine test injection via pointer override**: Rather than refactoring every
   subsystem to accept dependency injection (which the plan acknowledges as "scope
   creep"), we use a simple `g_test_instance` pointer. When non-null, `Instance()`
   returns the test instance. This is a minimal, well-understood pattern that
   unblocks test isolation without a cascading refactor.

5. **Fuzz targets with dual entry points**: Each fuzz target supports both libFuzzer
   (`LLVMFuzzerTestOneInput`) and standalone mode (`main()` reading from stdin), so
   they work on both Linux/clang (CI fuzz runner) and Windows/MSVC (local development).

## Already Built (During P0-2 through P0-1)

The following P0-3 requirements were already implemented alongside the individual P0 plans:

- Catch2 v3 via FetchContent in `src/tests/CMakeLists.txt`
- 12 C++ unit test files (buffer, config, timer, scriptvm, sandbox, lual_error_safety,
  message_limits, runinloop_safety, engine, length_prefixed_codec, entity, client_api)
- 4 integration tests (client_tcp, network/tcp, network/udp, physics)
- 11 Lua test files (harness + async_helper + module tests)
- 6 performance benchmarks (buffer, config, msgpack, scriptvm, tcp_throughput, timer)
- 5 smoke tests (config_load, engine_init, lua, scriptvm, tcp_loopback)
- 8 test fixtures (config, event_loop, log, network, script_vm, test_helpers, timer, wsa)
- CI pipeline (`.github/workflows/ci.yml`) with smoke/unit/integration/lua/benchmark jobs
- Nightly pipeline (`.github/workflows/nightly.yml`) with coverage, Ubuntu GCC build
- Google Benchmark integration (optional, `CLOUDENGINE_BUILD_BENCHMARKS`)
- `.clang-format` (existed before this plan)
- All 28 CTest-registered tests: 12 unit + 2 integration + 9 lua + 5 smoke

## Acceptance Criteria

- [x] 5 fakes implemented and ready for use in test isolation
- [x] 3 C++ test utilities (scope_guard, async_helper, lua_assertions)
- [x] `.clang-tidy` configured with baseline checks (bugprone, performance, modernize, cppcoreguidelines, readability)
- [x] `resources/.luacheckrc` configured for Lua code linting
- [x] Engine supports `SetInstanceForTesting()` / `ClearTestInstance()` for test isolation
- [x] 4 fuzz targets created (network_frame, config_parse, msgpack_decode, lua_dostring)
- [x] Build succeeds with no new warnings
- [x] All 28 regression tests pass (100%)

## Deferred to Future Plans

- Contract tests verifying fake and real implementations produce identical results (risk: fake drift)
- Fuzz targets wired into CI (requires Linux/clang runner with libFuzzer or AFL++)
- CI quality gates: clang-tidy new-warnings enforcement, luacheck enforcement, clang-format compliance check
- FakeDatabaseService, FakePhysicsSystem, FakeConfigManager (need stable interfaces first)
- P1/P2 unit tests for subsystems not yet implemented (physics_thread, coroutine_scheduler, connector_retry, script_reloader, space)
