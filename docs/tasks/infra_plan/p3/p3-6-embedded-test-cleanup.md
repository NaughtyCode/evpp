# P3-6: Embedded Test Code Cleanup — Remove DB Smoke Test from engine.cc

## Objective

Remove the database smoke test embedded in `Engine::Start()` and move it to a proper test file.

## Current State

`engine.cc:249-279` contains a DB service smoke test embedded in production code:

```cpp
/* #if !defined(NDEBUG) */
/* DB Service smoke test — sends a test request and polls for result */
/* Uses shared_ptr self-referencing InvokeTimerPtr pattern for polling */
/* If callback never fires, shared_ptr cycle never breaks → leak */
/* #endif */
```

This is test logic in a production code path (`Engine::Start()`). It runs every Debug build startup, uses a self-referencing `shared_ptr` for polling, and can leak if the callback never fires.

## Implementation Steps

### Step 1: Move Test to Test File

**File**: `src/tests/integration/db_smoke_test.cc` (created by P0-3 test infrastructure)

```cpp
TEST(DatabaseSmokeTest, BasicInsertAndFind) {
    Engine engine;  /* test constructor with mock/in-memory DB */
    engine.Init(test_config);
    engine.Start();

    /* Send a test request */
    /* Wait for response */
    /* Verify result */

    engine.Shutdown();
}
```

### Step 2: Remove from engine.cc

**File**: `src/runtime/engine/engine.cc`

Delete lines 249-279 (the `#if !defined(NDEBUG)` block containing the smoke test).

### Step 3: Remove self-referencing shared_ptr Pattern

If the polling pattern is useful for async test helpers, extract it to a test utility:

**File**: `src/tests/test_utils/async_helper.h`

```cpp
/* Safe async test helper — no self-referencing shared_ptr */
class AsyncTestHelper {
public:
    template<typename Fn>
    void WaitFor(Fn condition, int timeout_ms = 5000);
};
```

## Acceptance Criteria

1. DB smoke test code removed from `engine.cc`
2. Equivalent test exists in `src/tests/integration/db_smoke_test.cc`
3. Production `Engine::Start()` contains no test logic
4. `#if !defined(NDEBUG)` test guard removed from engine.cc
5. CI runs the new test

## Dependencies: P0-3 (Test Infrastructure — creates the test directory) | Estimated Effort: ~50 lines
