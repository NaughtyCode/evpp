# P0-3: Test Infrastructure

## Objective

Establish a comprehensive test infrastructure covering ScriptVM, TimerManager, entity system, Lua modules, and integration scenarios. Move from zero engine-layer tests to a multi-level test pyramid.

## Current State

- **evpp layer**: ~15 test files in `src/tests/unit/` (buffer, event_loop, tcp, http, udp, timer, sockets, dns)
- **engine layer**: **0 test files** — no tests for engine.cc, vm.cc, config.cc, timer_manager, script bindings, database service, or physics
- **Lua tests**: Only 2 files — `test_net_client_server.lua` (458 lines), `msgpack_test.lua`
- **No mock infrastructure**: No mock EventLoop, TimerManager, or database
- **Development workflow**: 76+ `fprintf(stderr, ...)` calls suggest "add print → run manually → check output"

## Root Cause

1. `Engine::Instance()` singleton prevents test isolation
2. C++ binding layer tightly coupled to Lua state — testing ScriptVM requires full Lua environment
3. No mock/test double infrastructure
4. No test-first design culture

## Impact

For infrastructure software, zero engine-layer tests means zero reliability guarantees. Users cannot trust that:
- Timers won't silently leak
- Network bindings won't crash on edge cases
- Database requests won't be lost under concurrency
- Config reload won't break running subsystems

## Implementation Steps

### Step 1: Decouple Engine Singleton for Testability

**File**: `src/runtime/engine/engine.h`

Add a test constructor that accepts dependencies:

```cpp
class Engine {
public:
    /* Production constructor — uses real subsystems */
    Engine();

    /* Test constructor — accepts mock/fake subsystems */
    Engine(std::unique_ptr<EventLoop> event_loop,
           std::unique_ptr<TimerManager> timer_manager);

    static Engine& Instance();
    /* ... */
};
```

Add `SetInstanceForTesting()` to temporarily replace the singleton.

### Step 2: ScriptVM Unit Tests

**File**: `src/tests/unit/script_vm_test.cc`

```cpp
TEST(ScriptVMTest, CreateAndDestroy) {
    ScriptVM vm;
    EXPECT_TRUE(vm.GetState() != nullptr);
}

TEST(ScriptVMTest, DoString) {
    ScriptVM vm;
    bool ok = vm.DoString("x = 42");
    EXPECT_TRUE(ok);
    /* Verify x == 42 via lua_getglobal */
}

TEST(ScriptVMTest, DoString_SyntaxError) {
    ScriptVM vm;
    bool ok = vm.DoString("invalid syntax @@@");
    EXPECT_FALSE(ok);
}

TEST(ScriptVMTest, DoFile_NotFound) {
    ScriptVM vm;
    bool ok = vm.DoFile("nonexistent.lua");
    EXPECT_FALSE(ok);
}

TEST(ScriptVMTest, UpdateScript_LifecycleHooks) {
    ScriptVM vm;
    /* Register InitScript/UpdateScript/DestroyScript */
    vm.DoString(R"(
        init_called = false
        update_called = false
        destroy_called = false
        function InitScript() init_called = true end
        function UpdateScript() update_called = true end
        function DestroyScript() destroy_called = true end
    )");
    vm.InitScript();
    EXPECT_TRUE(/* init_called */);
    vm.UpdateScript();
    EXPECT_TRUE(/* update_called */);
    vm.DestroyScript();
    EXPECT_TRUE(/* destroy_called */);
}

TEST(ScriptVMTest, StackCleanAfterError) {
    ScriptVM vm;
    int top_before = lua_gettop(vm.GetState());
    vm.DoString("error('boom')");
    int top_after = lua_gettop(vm.GetState());
    EXPECT_EQ(top_before, top_after);
}

TEST(ScriptVMTest, ImportScript_LoadsModule) {
    /* ... */
}

TEST(ScriptVMTest, ImportScript_CircularDependencyDetection) {
    /* ... */
}
```

### Step 3: TimerManager Unit Tests

**File**: `src/tests/unit/timer_manager_test.cc`

```cpp
TEST(TimerManagerTest, SingleShotTimer_Fires) {
    TimerManager tm;
    int counter = 0;
    tm.AddTimer(10, false, [&]() { counter++; });
    /* Advance time by 20ms */
    tm.Update(20);
    EXPECT_EQ(counter, 1);
}

TEST(TimerManagerTest, RepeatingTimer_FiresMultipleTimes) {
    TimerManager tm;
    int counter = 0;
    tm.AddTimer(10, true, [&]() { counter++; });
    tm.Update(35);
    EXPECT_EQ(counter, 3);
}

TEST(TimerManagerTest, CancelTimer_DoesNotFire) {
    TimerManager tm;
    int counter = 0;
    TimerId id = tm.AddTimer(10, false, [&]() { counter++; });
    tm.CancelTimer(id);
    tm.Update(20);
    EXPECT_EQ(counter, 0);
}

TEST(TimerManagerTest, CancelTimer_DuringCallback) {
    /* Timer cancels itself during callback */
}

TEST(TimerManagerTest, NoLeakAfterManyTimers) {
    /* Create and cancel 10000 timers, verify no memory growth */
}

TEST(TimerManagerTest, ZeroIntervalTimer) {
    /* Edge case */
}

TEST(TimerManagerTest, NegativeInterval_Rejected) {
    /* Error handling */
}
```

### Step 4: Entity System Unit Tests

**File**: `src/tests/unit/entity_test.cc`

(See P0-1 plan for test cases)

### Step 5: Network Binding Unit Tests

**File**: `src/tests/unit/net_bind_test.cc`

- Test ConnCtx lifecycle (create → use → dispose)
- Test lightuserdata ↔ ConnCtx pointer round-trip
- Test `__gc` metamethod triggers cleanup
- Test `disposed` flag prevents use-after-free
- Test `luaL_error` paths with RAII objects on stack

### Step 6: Lua Test Framework

**File**: `src/tests/lua/test_runner.lua`

A minimal Lua test framework:

```lua
local TestRunner = {}

function TestRunner:new(name)
    return setmetatable({name = name, tests = {}, passed = 0, failed = 0}, {__index = self})
end

function TestRunner:test(name, fn)
    table.insert(self.tests, {name = name, fn = fn})
end

function TestRunner:run()
    print("[" .. self.name .. "]")
    for _, t in ipairs(self.tests) do
        local ok, err = pcall(t.fn)
        if ok then
            self.passed = self.passed + 1
            print("  PASS: " .. t.name)
        else
            self.failed = self.failed + 1
            print("  FAIL: " .. t.name .. " - " .. tostring(err))
        end
    end
    print(string.format("  %d passed, %d failed", self.passed, self.failed))
    return self.failed == 0
end

return TestRunner
```

### Step 7: Lua Module Tests

**File**: `src/tests/lua/class_test.lua` — test class.lua inheritance, isinstanceof, hot-reload behavior, GC interaction

**File**: `src/tests/lua/timer_test.lua` — test Lua timer API

**File**: `src/tests/lua/import_test.lua` — test ScriptImporter path resolution, circular dependency

**File**: `src/tests/lua/log_test.lua` — test log level filtering

**File**: `src/tests/lua/net_encapsulation_test.lua` — test server.lua and client.lua wrappers

### Step 8: Integration Tests

**File**: `src/tests/integration/engine_lifecycle_test.cc`

- Engine::Init → Start → Run (briefly) → Shutdown
- Verify no crashes, no leaks (valgrind/ASAN clean)
- Verify all subsystems initialize and shut down in order

**File**: `src/tests/integration/script_to_cpp_test.cc`

- Lua script triggers C++ callback
- C++ callback modifies Lua state
- Full round-trip with error handling

### Step 9: CMake Integration

**File**: `src/tests/CMakeLists.txt`

Add test targets:
```cmake
add_executable(test_script_vm unit/script_vm_test.cc)
target_link_libraries(test_script_vm evpp_runtime gtest)

add_executable(test_timer_manager unit/timer_manager_test.cc)
target_link_libraries(test_timer_manager evpp_runtime gtest)

# ... etc.

enable_testing()
add_test(NAME ScriptVM COMMAND test_script_vm)
add_test(NAME TimerManager COMMAND test_timer_manager)
```

### Step 10: CI Configuration

**File**: `.github/workflows/ci.yml` (or equivalent CI config)

- Build and run all tests on push/PR
- Run with ASAN/UBSAN on Linux
- Run valgrind leak check
- Collect coverage metrics

## Acceptance Criteria

1. ScriptVM unit tests exist and pass (≥8 test cases)
2. TimerManager unit tests exist and pass (≥8 test cases)
3. Entity system unit tests exist and pass (see P0-1 criteria)
4. Network binding unit tests exist and pass (≥5 test cases)
5. Lua test framework is functional and reusable
6. Lua module tests exist for class, timer, import, log, net (≥3 test cases each)
7. Integration tests cover engine lifecycle and script↔C++ round-trip
8. All tests run via `cmake --build . --target test` (or `ctest`)
9. CI pipeline runs tests on every push
10. ASAN/UBSAN clean on Linux

## Dependencies

- P0-1 (Entity Model) — entity tests depend on entity implementation
- P0-2 (Message Framing) — network binding tests may depend on framing

## Estimated Effort

- ScriptVM tests: ~300 lines
- TimerManager tests: ~250 lines
- Entity tests: ~200 lines
- Network bind tests: ~250 lines
- Lua test framework: ~80 lines
- Lua module tests: ~400 lines (5 files)
- Integration tests: ~200 lines
- CMake/CI config: ~100 lines
- **Total**: ~2000 lines

## Risks

- Singleton decoupling may require significant refactoring; start with the minimal change needed for testability
- Lua tests require a running ScriptVM instance; tests must clean up properly to avoid cross-test contamination
- CI setup may require platform-specific tuning (Windows vs Linux signal handling, ASAN availability)
