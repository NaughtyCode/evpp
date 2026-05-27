# P0-3: Comprehensive Automated Testing Strategy

## Objective

Establish a complete, multi-level automated testing infrastructure covering C++ unit tests, Lua module tests, integration tests, end-to-end tests, performance benchmarks, fuzz testing, and static analysis. Move from zero engine-layer automated tests to a production-grade quality assurance system.

## Current State

- **C++ tests**: 0 files — `src/tests/` was deleted (commit 1842bd07). CMake test targets no longer exist. No test framework (GoogleTest, Catch2, or doctest) is integrated.
- **Lua tests**: 2 files in `resources/script/tests/` — `test_net_client_server.lua` (459 lines, 5 async net tests) and `msgpack_test.lua` (617 lines, 22 unit tests). Plus 1 shared harness at `tests/harness/test_harness.lua` (78 lines) providing `assert_truthy`, `assert_eq`, `assert_gt`, `assert_contains`. No proper test framework (busted, luassert, lunit).
- **No mock/fake infrastructure**: No mock EventLoop, fake TimerManager, in-memory database, or fake physics system exists. `Engine::Instance()` singleton makes test isolation impossible.
- **No CI/CD**: All builds and tests run manually on developer machines. The CMakeLists.txt has no `ENABLE_TESTS` option or test targets.
- **No static analysis**: No clang-tidy, cppcheck, or SonarQube integration.
- **No performance testing**: No benchmark framework, no performance regression detection.
- **No fuzz testing**: No fuzz targets for msgpack parsing, network protocol, or config parsing.
- **No coverage tracking**: No code coverage measurement exists.
- **Development workflow**: 70 `fprintf(stderr, ...)` calls suggest "add print → run manually → check output" debugging.

## Root Cause

1. `Engine::Instance()` singleton prevents test isolation — subsystems cannot be tested independently
2. C++ binding layer tightly coupled to Lua state — testing ScriptVM requires full Lua environment
3. No mock/test double infrastructure — impossible to test components in isolation
4. No test-first design culture — tests were an afterthought, leading to the deletion of `src/tests/`
5. Prototype-origin codebase — testing was deferred indefinitely for speed

## Impact

Without automated testing, the engine provides zero reliability guarantees. Users cannot trust that:
- Timers won't silently leak under concurrent create/cancel
- Network bindings won't crash on edge cases (malformed data, connection storms)
- Database requests won't be lost or corrupted under concurrent load
- Config reload won't break running subsystems
- Physics simulation produces deterministic results across platforms
- A refactoring or dependency upgrade won't introduce silent regressions

For infrastructure software intended to carry real game business, this is a blocking defect.

---

## Test Strategy Overview: The Test Pyramid

```
        ╱  E2E   ╲          Manual + Automated: full engine with real network/DB
       ╱──────────╲         ~10-20 scenarios, run nightly
      ╱ Integration╲        Subsystem pairs: ScriptVM↔Timer, Engine↔Network, DB↔Physics
     ╱──────────────╲       ~50-80 scenarios, run per-PR
    ╱   Unit Tests   ╲      C++ (GoogleTest) + Lua (extended harness)
   ╱──────────────────╲     ~300+ test cases, run per-commit
  ╱  Static Analysis   ╲    clang-tidy + cppcheck + luacheck
 ╱──────────────────────╲   Zero-config, run per-commit
```

| Layer | Scope | Framework | Count Target | Run Frequency | Max Runtime |
|-------|-------|-----------|-------------|---------------|-------------|
| Static Analysis | Every source file | clang-tidy, cppcheck, luacheck | N/A | Per-commit (pre-commit hook) | < 30s |
| Unit (C++) | Single function/class | GoogleTest | ≥ 300 | Per-commit (CI push) | < 60s |
| Unit (Lua) | Single module/function | Extended test harness | ≥ 150 | Per-commit (CI push) | < 30s |
| Integration | Subsystem pairs | GoogleTest + Lua harness | ≥ 50 | Per-PR | < 5 min |
| E2E | Full engine flows | Custom runner | ≥ 10 | Nightly + release tag | < 30 min |
| Performance | Hot paths + benchmarks | Google Benchmark | ≥ 20 | Nightly | < 60 min |
| Fuzz | msgpack, config, network | libFuzzer / AFL++ | ≥ 5 targets | Nightly (continuous) | 4-24 hr |

---

## Phase 1: Unit Test Foundation

### 1.1 C++ Test Framework: GoogleTest

**Rationale**: GoogleTest is the industry standard for C++ testing. It provides:
- Rich assertion macros (`EXPECT_EQ`, `ASSERT_THROW`, `EXPECT_NEAR`)
- Test fixtures with `SetUp()`/`TearDown()` for shared state
- Death tests for verifying `assert()`/`abort()` transitions (see P2-19)
- Value-parameterized tests for combinatorial edge cases
- Integration with `ctest` and CI reporters (JUnit XML, JSON)
- Mock framework (`gmock`) for test doubles

**Alternative considered**: Catch2 — more header-only and self-contained, but smaller ecosystem. GoogleTest is preferred due to gmock integration and broader CI tooling support.

### 1.2 Decouple Engine Singleton for Testability

The critical blocker: `Engine::Instance()` returns a global singleton. Tests need isolated Engine instances.

**Solution**: Dependency injection via constructor overloading + `SetInstanceForTesting()`:

```cpp
class Engine {
public:
    Engine();  // production: uses real subsystems

    // Test constructor: accepts mock/fake dependencies
    Engine(std::unique_ptr<EventLoop> event_loop,
           std::unique_ptr<TimerManager> timer_manager,
           std::unique_ptr<ConfigManager> config_manager);

    static Engine& Instance();
    static void SetInstanceForTesting(Engine* test_instance);
    static void ClearTestInstance();

private:
    static Engine* test_instance_;  // non-null during test execution
};
```

This enables subcomponent isolation without requiring every test to stand up the full engine.

### 1.3 Mock/Fake Infrastructure Design

A layered approach to test doubles:

| Layer | Type | Purpose | Example |
|-------|------|---------|---------|
| **Fake** | In-memory implementation | Full behavior, no side effects | `FakeTimerManager` — stores timers in a vector, `Update(dt)` fires them |
| **Mock** | Expectation-based (gmock) | Verify interactions | `MockEventLoop` — verifies `RunInLoop` was called with correct callback |
| **Stub** | Hardcoded responses | Return canned data | `StubDatabase` — returns preset query results |
| **Spy** | Recording passthrough | Observe calls | `SpyTCPConn` — records all `Send()` calls and their arguments |

**Key fakes to implement**:

```
src/tests/fakes/
├── fake_event_loop.h           # Queues RunInLoop callbacks; manual Pump() to drain
├── fake_timer_manager.h        # Virtual time: Update(dt) fires due timers
├── fake_config_manager.h       # In-memory config; no file I/O
├── fake_database_service.h     # Queues DB requests; manual Flush() to drain
├── fake_physics_system.h       # Records commands; manual ProduceResult() to respond
├── fake_tcp_connection.h       # Buffers sent data; manual DeliverMessage() to receive
├── fake_network_clock.h        # Controllable time for deterministic network tests
└── fake_random_source.h        # Seedable deterministic RNG for reproducible tests
```

Each fake implements the same interface as its real counterpart, making it a drop-in replacement in test constructors.

### 1.4 Test Organization

```
src/tests/
├── CMakeLists.txt                 # Test build configuration
├── unit/
│   ├── script_vm_test.cc          # ScriptVM lifecycle, DoString, DoFile, error paths
│   ├── timer_manager_test.cc      # Create, cancel, repeat, time-advance
│   ├── config_manager_test.cc     # Load, reload, callback, validation
│   ├── buffer_test.cc             # Append/read, Reserve, NextAllString, int64 endian
│   ├── length_prefixed_codec_test.cc  # Encode/decode, sticky/fragmented packets
│   ├── msgpack_codec_test.cc      # Encode depth/size checks, type roundtrips
│   ├── event_loop_test.cc         # RunInLoop ordering, drain-on-shutdown
│   ├── connector_test.cc          # Connect, retry, backoff, timeout
│   ├── entity_test.cc             # Lifecycle, attributes, components, timers
│   ├── space_test.cc              # VM isolation, entity routing, cross-space messaging
│   ├── net_lifetime_test.cc       # NetAliveGuard, PendingRefTracker
│   ├── sandbox_test.cc            # Library whitelist, dangerous function removal
│   ├── lual_error_safety_test.cc  # RAII destructor on error paths
│   ├── physics_thread_test.cc     # Enqueue, process, shutdown, CV wakeup
│   ├── coroutine_scheduler_test.cc # Create, resume, cancel, error propagation
│   └── script_reloader_test.cc    # Validate, reload, rollback, debounce
├── integration/
│   ├── engine_lifecycle_test.cc   # Init→Start→Run→Shutdown, verify ordering
│   ├── script_to_cpp_test.cc      # Lua→C++→Lua roundtrip with error handling
│   ├── network_echo_test.cc       # TCP client↔server with real loopback
│   ├── db_crud_test.cc            # Insert→Find→Update→Delete with fake DB
│   ├── physics_integration_test.cc # Spawn→Tick→FetchResult cycle
│   ├── hot_reload_test.cc         # Modify file→detect→validate→reload→verify
│   ├── multi_vm_test.cc           # 2+ spaces, message passing, isolation
│   └── shutdown_safety_test.cc    # In-flight messages + rapid shutdown = no crash
├── e2e/
│   ├── full_engine_test.cc        # Start engine, run game loop, verify all subsystems
│   ├── multi_client_test.cc       # 100 concurrent clients, stress message patterns
│   ├── crash_recovery_test.cc     # Simulate crashes, verify clean restart
│   └── long_running_test.cc       # 24-hour soak test (nightly)
├── performance/
│   ├── buffer_benchmark.cc        # Append/Read throughput at various sizes
│   ├── msgpack_benchmark.cc       # Encode/decode throughput, depth scaling
│   ├── timer_benchmark.cc         # Timer create/cancel ops/sec, fire latency
│   ├── network_benchmark.cc       # Messages/sec throughput, latency percentiles
│   ├── entity_benchmark.cc        # Create/destroy ops/sec, attribute read/write
│   └── physics_benchmark.cc       # Rigid body simulation step time
├── fuzz/
│   ├── msgpack_decode_fuzz.cc     # Fuzz msgpack binary decoding
│   ├── config_parse_fuzz.cc       # Fuzz JSON config parsing
│   ├── network_frame_fuzz.cc      # Fuzz length-prefixed frame decoder
│   └── lua_dostring_fuzz.cc       # Fuzz Lua code execution under sandbox
├── lua/
│   ├── test_runner.lua            # Proper test framework (structured output)
│   ├── class_test.lua             # Inheritance, isinstanceof, hot-reload
│   ├── timer_test.lua             # Create, cancel, repeat, delay accuracy
│   ├── import_test.lua            # Path resolution, circular dependency
│   ├── log_test.lua               # Level filtering, formatting
│   ├── net_test.lua               # Server/client encapsulation, broadcast
│   ├── async_test.lua             # Coroutine await, sleep, error propagation
│   ├── entity_test.lua            # Create, attributes, components, connection
│   ├── sandbox_test.lua           # Restricted library access
│   ├── config_test.lua            # Read, reload, callback
│   ├── db_test.lua                # Query, insert, pagination, error handling
│   └── physics_test.lua           # Spawn, apply force, fetch result
├── fakes/                         # (as listed above)
├── fixtures/                      # Test data
│   ├── sample_config.json         # Minimal valid config for test use
│   ├── sample_scripts/            # Minimal Lua scripts for import tests
│   │   ├── simple_module.lua
│   │   ├── circular_a.lua
│   │   ├── circular_b.lua
│   │   └── syntax_error.lua
│   ├── sample_msgpack/            # Known-good binary MessagePack fixtures
│   │   ├── fixnum_42.bin
│   │   ├── array_100.bin
│   │   └── map_nested.bin
│   └── sample_network/            # Captured packet traces for replay tests
└── test_utils/                    # Shared test utilities
    ├── async_helper.h             # WaitFor(predicate, timeout) helper
    ├── lua_assertions.h           # C++ helpers to inspect Lua state
    ├── scope_guard.h              # Deferred cleanup for test isolation
    └── test_main.cc               # GoogleTest entry point
```

---

## Phase 2: Lua Test Framework

### 2.1 Test Framework Upgrade

The existing `tests/harness/test_harness.lua` provides basic assertions (`assert_eq`, `assert_truthy`, `assert_gt`, `assert_contains`) but lacks:

- **Structured output**: Test results should emit machine-readable format (JSON/TAP) for CI consumption
- **Test discovery**: Tests should be auto-discovered by module, not manually chained
- **Setup/teardown**: Per-test isolation (clean global state before each test)
- **Async test support**: First-class support for callback-driven tests (network, DB, timer)
- **Skipping**: `SKIP("reason")` for platform-specific or not-yet-implemented tests
- **Tagging**: `@slow`, `@network`, `@flaky` annotations for selective execution

**Upgraded harness** (`src/tests/lua/test_runner.lua`):

```lua
local runner = {}

function runner.register(name, tags, fn)
    -- tags: table like {slow=true, network=true}
    -- fn: receives a TestContext with assert methods + async helpers
end

function runner.run(filter_tags)
    -- Auto-discovers all registered tests
    -- Runs them in order, respecting skip conditions
    -- Outputs JSON results to stdout for CI parsing
end

-- Async testing API:
-- test:wait_for(condition_fn, timeout_ms)  -- polls until condition true or timeout
-- test:on_timer(ms, callback)              -- schedule a timer from within test
-- test:sleep(ms)                           -- cooperative sleep (coroutine yield)
```

### 2.2 Lua Module Test Isolation

Each Lua test file must create a clean environment. The test harness should:

1. Take a snapshot of `_G` before the test
2. Run the test
3. Restore `_G` to snapshot (removing any globals the test set)
4. Clear `package.loaded` for the modules under test

This prevents cross-test contamination and makes test ordering irrelevant.

---

## Phase 3: Integration & E2E Testing

### 3.1 Integration Test Strategy

Integration tests verify that subsystem pairs work together correctly. Each integration test:

1. Creates **real** instances of two subsystems
2. Uses **fakes** for all other dependencies
3. Tests the communication contract between them

Key integration test scenarios:

| Subsystem Pair | Test Scenario | What It Validates |
|----------------|--------------|-------------------|
| Engine + Network | Start TCP server, connect client, send messages | Engine lifecycle, network bindings, event dispatch |
| Engine + Timer | Register repeating timer, run engine loop | Timer fires in Update(), cancels on Shutdown |
| Engine + DB | Insert, query, paginate through engine | DB service init, queue draining, result delivery |
| Engine + Physics | Spawn body, tick, fetch result | Physics thread startup, command dispatch, result retrieval |
| ScriptVM + Network | Lua calls net.server.listen, receive message | C++→Lua callback dispatch, error handling |
| ScriptVM + DB | Lua sends DB query, receives result callback | DB binding marshaling, async response delivery |
| ScriptVM + Timer | Lua creates timer, callback fires | Timer binding, Lua function ref lifecycle |
| Config + All Subsystems | Reload config, verify each subsystem re-reads | Hot-reload notification, callback ordering |
| Space + Entity | Create space, add entities, route messages | VM isolation, entity routing, cross-space messaging |
| Sandbox + ScriptVM | Load restricted libraries, execute Lua | Whitelist enforcement, blocked function errors |

### 3.2 E2E Test Strategy

E2E tests run the full engine with minimal faking (real loopback network, real embedded DB):

```
E2E Test: Full Engine Lifecycle
  1. Engine::Init(config)
  2. Engine::Start()
  3. Run game loop for N frames (10-1000)
  4. Simulate player connect → authenticate → join space → send messages → disconnect
  5. Engine::Shutdown()
  6. Verify: no crashes, no leaks (ASAN), all subsystems clean up

E2E Test: Multi-Client Stress
  1. Start engine with TCP server on loopback
  2. Spawn 100 virtual clients, each connecting and sending 100 messages
  3. Verify: all messages received, no data corruption, connection counts correct
  4. Shutdown with messages in-flight, verify no crash

E2E Test: Crash Recovery
  1. Start engine, insert DB records
  2. Simulate crash (kill process)
  3. Restart engine with same DB
  4. Verify: records intact, no corruption, clean startup
```

E2E tests run **nightly** (not per-commit) due to their runtime. Some may run per-PR if fast enough (< 5 min).

---

## Phase 4: Performance Testing

### 4.1 Benchmark Framework

Use **Google Benchmark** for C++ microbenchmarks:

```cpp
#include <benchmark/benchmark.h>

static void BM_Buffer_AppendInt32(benchmark::State& state) {
    Buffer buf;
    for (auto _ : state) {
        buf.AppendInt32(0x12345678);
        buf.Reset();
    }
}
BENCHMARK(BM_Buffer_AppendInt32);
```

### 4.2 Performance Regression Detection

Each benchmark stores a baseline `.json` result in the repo:

```
src/tests/performance/baselines/
├── buffer_baseline.json
├── msgpack_baseline.json
├── timer_baseline.json
└── network_baseline.json
```

CI compares current results against baselines. A regression of > 10% (configurable threshold) triggers a **warning** on PR; > 25% triggers a **blocking failure**.

### 4.3 Hot-Path Benchmarks

Benchmarks target the identified hot paths:

| Benchmark | Metric | Target |
|-----------|--------|--------|
| `Buffer::AppendInt32` | ops/sec | > 50M/s |
| `Buffer::ReadInt32` | ops/sec | > 50M/s |
| `msgpack::pack(small_table)` | ops/sec | > 1M/s |
| `msgpack::unpack(small_msg)` | ops/sec | > 1M/s |
| `LengthPrefixedCodec::Decode` | MB/s | > 500 MB/s |
| `TimerManager::AddTimer` | ops/sec | > 10M/s |
| `TimerManager::Update(1000_timers)` | µs/frame | < 500µs |
| `Entity::Create` | ops/sec | > 1M/s |
| `Entity::GetAttr` | ns/op | < 100ns |
| `EventLoop::RunInLoop` (same-thread) | ns/op | < 500ns |
| `EventLoop::RunInLoop` (cross-thread) | µs/op | < 50µs |
| `PhysicsSystem::StepWorld(60fps)` | ms/frame | < 5ms |
| `ConfigManager::GetConfig` (contended) | ns/op | < 200ns |

### 4.4 Latency Distribution Testing

For network and timer paths, measure **p50/p99/p999** latency, not just mean:

```cpp
// Collect latency samples over 100k operations
// Verify: p99 < 1ms for timer dispatch, p999 < 10ms for network dispatch
```

---

## Phase 5: Security & Robustness Testing

### 5.1 Fuzz Testing

Use **libFuzzer** (via clang) for coverage-guided fuzz testing:

```
Fuzz Targets:
├── msgpack_decode_fuzz.cc    # Feed arbitrary bytes to msgpack decoder
├── config_parse_fuzz.cc      # Feed arbitrary JSON to config parser
├── network_frame_fuzz.cc     # Feed arbitrary bytes to length-prefixed decoder
├── lua_dostring_fuzz.cc      # Feed arbitrary strings to Lua DoString (sandboxed)
└── bson_parse_fuzz.cc        # Feed arbitrary bytes to BSON parser (if DB enabled)
```

Each fuzz target runs continuously in CI (dedicated fuzz runner, 4-24 hours per run). Coverage corpus is stored and replayed on each run.

### 5.2 Static Analysis

| Tool | Scope | Configuration | Enforcement |
|------|-------|---------------|-------------|
| **clang-tidy** | All C++ source | `src/.clang-tidy` — modernize, performance, bugprone, cppcoreguidelines checks | Blocking on new warnings |
| **cppcheck** | All C++ source | `src/.cppcheck-suppressions` | Advisory (report only) |
| **luacheck** | All Lua source | `resources/.luacheckrc` — unused vars, globals access, shadowing | Blocking on new warnings |
| **SonarQube** (optional) | All source | Server-side configuration | Dashboard tracking |

**`.clang-tidy` baseline configuration**:

```yaml
Checks: >
  -*,
  bugprone-*,
  performance-*,
  modernize-*,
  cppcoreguidelines-*,
  -modernize-use-trailing-return-type,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-type-vararg,
  readability-*,
  -readability-magic-numbers,
  -readability-identifier-length
```

### 5.3 Sanitizer Builds

Every CI run includes multiple sanitizer configurations:

| Sanitizer | Platform | What It Catches | Build Time Impact |
|-----------|----------|-----------------|-------------------|
| **ASAN** | Linux (clang) | Heap/stack buffer overflow, use-after-free, double-free, memory leaks | 2x slower runtime |
| **UBSAN** | Linux (clang) | Undefined behavior: signed overflow, misaligned access, null deref | 1.2x slower runtime |
| **TSAN** | Linux (clang) | Data races, deadlocks, lock order inversion | 5-15x slower runtime |
| **MSAN** | Linux (clang) | Uninitialized memory reads | 3x slower runtime |

TSAN is run nightly (too slow for per-commit). ASAN+UBSAN run on every PR.

### 5.4 Deterministic Testing

For reproducible test results:

- **Virtual time**: `FakeTimerManager` uses a controllable clock — tests advance time deterministically
- **Seeded randomness**: All RNG uses a test-provided seed, logged on failure for reproduction
- **Ordered execution**: `FakeEventLoop` processes callbacks in FIFO order with explicit `Pump()` calls (no real threading)
- **Controlled concurrency**: Threaded integration tests use barriers and fixed thread scheduling to reproduce race conditions deterministically

---

## Phase 6: CI/CD Integration

### 6.1 CMake Test Configuration

```cmake
# src/tests/CMakeLists.txt
option(ENABLE_TESTS "Build test targets" OFF)

if(ENABLE_TESTS)
    enable_testing()

    # GoogleTest
    find_package(GTest REQUIRED)
    include(GoogleTest)

    # Common test libraries
    add_library(test_fakes STATIC
        fakes/fake_event_loop.cc
        fakes/fake_timer_manager.cc
        fakes/fake_config_manager.cc
        fakes/fake_database_service.cc
        fakes/fake_physics_system.cc
        fakes/fake_tcp_connection.cc
    )
    target_link_libraries(test_fakes PUBLIC CloudEngine GTest::gmock)

    # Unit tests — add per-file
    function(add_engine_test name)
        add_executable(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE CloudEngine test_fakes GTest::gtest_main)
        gtest_discover_tests(${name}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
            DISCOVERY_TIMEOUT 30
        )
    endfunction()

    add_engine_test(test_script_vm unit/script_vm_test.cc)
    add_engine_test(test_timer_manager unit/timer_manager_test.cc)
    add_engine_test(test_config_manager unit/config_manager_test.cc)
    add_engine_test(test_buffer unit/buffer_test.cc)
    add_engine_test(test_codec unit/length_prefixed_codec_test.cc)
    # ... etc.

    # Integration tests — may take longer
    function(add_integration_test name)
        add_executable(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE CloudEngine test_fakes GTest::gtest_main)
        set_property(TEST ${name} PROPERTY LABELS "integration")
        set_property(TEST ${name} PROPERTY TIMEOUT 120)
        gtest_discover_tests(${name})
    endfunction()

    add_integration_test(test_engine_lifecycle integration/engine_lifecycle_test.cc)
    # ... etc.

    # Performance benchmarks
    option(ENABLE_BENCHMARKS "Build benchmark targets" OFF)
    if(ENABLE_BENCHMARKS)
        find_package(benchmark REQUIRED)
        function(add_benchmark name)
            add_executable(${name} ${ARGN})
            target_link_libraries(${name} PRIVATE CloudEngine benchmark::benchmark)
        endfunction()
        add_benchmark(bench_buffer performance/buffer_benchmark.cc)
        # ... etc.
    endif()
endif()
```

### 6.2 CI Pipeline Architecture

```
Git Push → Pre-commit Hook (static analysis, < 30s)
         → CI Pipeline:
              ├── Build (Linux Debug, Linux Release, Windows Debug) [parallel]
              ├── Unit Tests (C++ + Lua) [parallel per-platform]
              ├── Integration Tests [sequential, per-PR]
              ├── Sanitizer Tests (ASAN + UBSAN) [per-PR]
              ├── Lint (clang-format, luacheck, check-fprintf) [per-PR]
              └── Nightly Pipeline:
                    ├── TSAN Tests
                    ├── MSAN Tests
                    ├── E2E Tests
                    ├── Performance Benchmarks (± regression report)
                    ├── Fuzz Tests (4hr continuous)
                    ├── Coverage Report
                    └── 24hr Soak Test
```

### 6.3 Quality Gates (Per-PR)

| Gate | Blocking? | Action on Failure |
|------|-----------|-------------------|
| Build (Linux Debug + Release) | Yes | PR blocked |
| Build (Windows Debug) | Yes | PR blocked |
| Unit Tests (all) | Yes | PR blocked |
| Integration Tests | Yes | PR blocked |
| ASAN + UBSAN clean | Yes | PR blocked |
| clang-tidy (new warnings) | Yes | PR blocked |
| luacheck (new warnings) | Yes | PR blocked |
| clang-format compliance | Yes | PR blocked |
| Code coverage decrease > 2% | Advisory | PR warning |
| Performance regression > 10% | Advisory | PR warning (nightly check) |
| Performance regression > 25% | Yes | PR blocked (nightly check) |

### 6.4 Test Execution Strategy

| Trigger | What Runs | Max Time |
|---------|-----------|----------|
| `git commit` (pre-commit hook) | clang-format check, luacheck on changed files | < 10s |
| `git push` to feature branch | Unit tests (C++ + Lua), quick integration tests | < 3 min |
| PR created/updated | Full unit + integration, sanitizers, lint | < 15 min |
| Merge to master | All of above + benchmarks, deploy to staging | < 30 min |
| Nightly (3am UTC) | E2E, fuzz, TSAN, MSAN, coverage, soak | < 8 hr |
| Release tag | Full nightly suite + manual QA checklist | 24 hr |

---

## Implementation Steps

### Step 1: Add GoogleTest Dependency + CMake Integration

**Files**: `CMakeLists.txt`, `src/tests/CMakeLists.txt`

- Add `ENABLE_TESTS` option to root `CMakeLists.txt`
- Add GoogleTest via FetchContent or find_package
- Create `src/tests/CMakeLists.txt` with the test infrastructure above

### Step 2: Decouple Engine Singleton

**File**: `src/runtime/engine/engine.h`, `engine.cc`

- Add test constructor accepting dependency injection
- Add `SetInstanceForTesting()` / `ClearTestInstance()`
- Add `#ifdef ENGINE_TESTING` guards if needed for friend access

### Step 3: Implement Fake Library

**Files**: `src/tests/fakes/*.h`, `src/tests/fakes/*.cc`

- Implement 8 core fakes (EventLoop, TimerManager, ConfigManager, DatabaseService, PhysicsSystem, TCPConnection, NetworkClock, RandomSource)
- Each fake is self-contained (~50-150 lines each)

### Step 4: Create Shared Test Utilities

**Files**: `src/tests/test_utils/*.h`

- `async_helper.h` — `WaitFor(predicate, timeout_ms)` for polling-based async tests
- `lua_assertions.h` — `ExpectLuaGlobal(L, "x", 42)`, `ExpectLuaStackClean(L)`
- `scope_guard.h` — `ScopeGuard` for cleanup in test teardown
- `test_main.cc` — GoogleTest `main()` entry point

### Step 5: Write C++ Unit Tests — Priority Order

| Priority | Test File | Approx. Cases | Rationale |
|----------|-----------|---------------|-----------|
| 1 | `script_vm_test.cc` | 15-20 | Core VM — everything depends on it |
| 2 | `timer_manager_test.cc` | 12-15 | Most widely used subsystem |
| 3 | `buffer_test.cc` | 10-12 | Network foundation — P0-2 prerequisite |
| 4 | `config_manager_test.cc` | 8-10 | Hot-reload depends on it |
| 5 | `event_loop_test.cc` | 8-10 | RunInLoop safety depends on it |
| 6 | `length_prefixed_codec_test.cc` | 10-12 | P0-2 framing |
| 7 | `msgpack_codec_test.cc` | 10-12 | Serialization foundation |
| 8 | `entity_test.cc` | 12-15 | P0-1 entity model |
| 9 | `net_lifetime_test.cc` | 8-10 | P0-6 RunInLoop safety |
| 10 | `sandbox_test.cc` | 10-12 | P0-7 sandbox |
| 11 | `lual_error_safety_test.cc` | 6-8 | P0-4 exception safety |
| 12 | `physics_thread_test.cc` | 8-10 | P1-4 + P1-5 |
| 13 | `coroutine_scheduler_test.cc` | 8-10 | P1-7 coroutines |
| 14 | `connector_test.cc` | 6-8 | P3-12 retry logic |
| 15 | `script_reloader_test.cc` | 8-10 | P1-8 hot-reload |
| 16 | `space_test.cc` | 8-10 | P1-9 multi-VM |

### Step 6: Write Integration Tests

Implement integration tests in the order subsystems are stabilized. Each integration test depends on the corresponding unit tests + the P0 plan for that subsystem.

### Step 7: Upgrade Lua Test Framework + Write Lua Tests

**Files**: `src/tests/lua/test_runner.lua`, `src/tests/lua/*.lua`

- Upgrade `test_harness.lua` to full framework with structured output
- Write per-module Lua tests (12 files, ~50 tests each minimum)

### Step 8: Add Static Analysis Configuration

**Files**: `.clang-tidy`, `.clang-format`, `resources/.luacheckrc`

- Create clang-tidy configuration with baseline checks
- Create clang-format configuration matching existing code style
- Create luacheck configuration for Lua code

### Step 9: Add CI Pipeline

**File**: `.github/workflows/ci.yml`

- Implement the CI pipeline defined in P3-2 (expanded) with the matrix above
- Add pre-commit hook script at `scripts/pre-commit.sh`

### Step 10: Add Performance Benchmarks

**Files**: `src/tests/performance/*.cc`, `src/tests/performance/baselines/*.json`

- Implement benchmarks for identified hot paths
- Generate and store baseline results

### Step 11: Add Fuzz Targets

**Files**: `src/tests/fuzz/*.cc`

- Implement 5 fuzz targets for untrusted input boundaries
- Configure CI fuzz runner

### Step 12: Add Coverage Configuration

**Files**: `CMakeLists.txt`, `.github/workflows/ci.yml`

- Add `-fprofile-arcs -ftest-coverage` or `--coverage` flags
- Integrate `gcovr` or `llvm-cov` for report generation
- Set initial coverage targets: ≥ 60% line coverage for runtime, ≥ 80% for critical paths (event loop, buffer, config)

---

## Acceptance Criteria

### Unit Tests
1. GoogleTest framework integrated and discoverable via `ctest`
2. Engine singleton supports test injection (`SetInstanceForTesting`)
3. All 8 core fakes implemented and usable
4. ≥ 200 C++ unit test cases across ≥ 12 test files
5. ≥ 100 Lua unit test cases across ≥ 8 test files
6. All unit tests run in < 60s total

### Integration & E2E
7. ≥ 15 integration test scenarios covering subsystem pairs
8. ≥ 4 E2E scenarios (lifecycle, multi-client, crash recovery, long-running)
9. Integration tests run in < 5 min total
10. E2E tests run nightly

### Static & Dynamic Analysis
11. clang-tidy integrated with baseline checks, zero new warnings enforced
12. luacheck integrated for Lua code
13. ASAN + UBSAN build passes all tests on Linux
14. TSAN build runs nightly (0 data races verified for critical paths)
15. Fuzz targets built and running continuously in CI

### Performance
16. ≥ 12 benchmark targets covering hot paths
17. Performance regression detection with ≥ 10% warning / ≥ 25% blocking thresholds
18. Baseline results committed to repo and version-controlled

### CI
19. CI pipeline runs on every push (unit + quick integration + lint)
20. Full pipeline runs on PR (includes sanitizers + all integration)
21. Nightly pipeline runs E2E + TSAN + fuzz + coverage + benchmarks
22. Quality gates enforced: build, tests, sanitizers, lint are blocking
23. CI badge in README

### Coverage
24. Code coverage measured per-PR
25. Coverage decrease > 2% triggers advisory warning
26. Initial coverage target: ≥ 60% line coverage

---

## Dependencies

- P0-1 (Entity Model) — entity tests depend on entity implementation
- P0-2 (Message Framing) — codec tests depend on LengthPrefixedCodec
- P0-4 (luaL_error Safety) — safety tests depend on scope-block fix
- P0-6 (RunInLoop Safety) — lifetime tests depend on NetAliveGuard
- P0-7 (Lua Sandbox) — sandbox tests depend on whitelist implementation
- P1-1 (Binding Boilerplate) — Lua tests benefit from unified binding pattern

---

## Estimated Effort

| Component | Lines | Complexity |
|-----------|-------|------------|
| CMake + GoogleTest integration | ~100 | Low |
| Engine singleton decoupling | ~80 | Medium |
| Fake library (8 fakes) | ~800 | Medium |
| Test utilities (async_helper, lua_assertions, scope_guard) | ~200 | Low |
| C++ unit tests (200+ cases) | ~3000 | Low-Medium |
| C++ integration tests (15+ scenarios) | ~1500 | Medium |
| C++ E2E tests (4 scenarios) | ~600 | High |
| Lua test framework upgrade | ~200 | Medium |
| Lua module tests (100+ cases) | ~2000 | Low-Medium |
| Static analysis config (.clang-tidy, .clang-format, .luacheckrc) | ~100 | Low |
| CI pipeline (.github/workflows/ci.yml) | ~200 | Medium |
| Performance benchmarks (12 benchmarks) | ~500 | Medium |
| Fuzz targets (5 targets) | ~200 | Medium-High |
| Coverage configuration | ~50 | Low |
| **Total** | **~9,500 lines** | |

---

## Risks

- **Singleton decoupling scope creep**: Making Engine testable may require changes to many subsystems that access `Engine::Instance()`. Mitigation: phase it — start with `SetInstanceForTesting()` hack, refactor to proper DI incrementally.
- **Flaky tests**: Network and timer tests that depend on real timing are inherently flaky. Mitigation: use virtual time (`FakeTimerManager`) and loopback networking for determinism; tag any real-time tests as `@flaky` and run them only in nightly CI.
- **Test maintenance cost**: ~9,500 lines of test code requires ongoing maintenance. Mitigation: treat test code with same quality standards as production code; refactor tests when refactoring production code; delete tests that no longer provide value.
- **CI infrastructure cost**: GitHub Actions free tier has limits (2000 min/month for private repos). Mitigation: optimize test parallelism; use self-hosted runners for nightly/heavy jobs if needed.
- **Fake drift**: Fakes that diverge from real implementations produce false-passing tests. Mitigation: each fake's interface must inherit from the real class; add contract tests that verify fake and real implementations produce identical results for the same inputs.
- **Test data bloat**: Binary fixtures (`.bin`, `.json`) in the repo increase clone time. Mitigation: keep fixtures small (< 10KB each); generate large test data programmatically in test setup.
