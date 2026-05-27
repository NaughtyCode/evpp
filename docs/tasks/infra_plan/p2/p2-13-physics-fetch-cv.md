# P2-13: PhysicsSystem::FetchResult — Replace Busy-Wait with condition_variable

## Objective

Replace the 100µs polling busy-wait in `PhysicsSystem::FetchResult` with a `std::condition_variable` to eliminate CPU waste and latency jitter.

## Current State

`physics_system.cc:264` uses a busy-wait spin loop:

```cpp
/* Poll for physics results with 100µs spin: */
while (!result_ready_ && elapsed < timeout) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    elapsed += 100;
}
```

This wastes CPU and adds latency jitter. Even though 100µs is short, it runs every frame — 60fps × 100µs spin = 6ms/second of wasted CPU time per frame.

## Root Cause

No notification mechanism from physics thread back to main thread. The main thread polls a flag set by the physics thread.

## Implementation Steps

### Step 1: Add condition_variable

**File**: `src/runtime/physics/physics_system.h`

```cpp
class PhysicsSystem {
    /* ... */
    std::mutex result_mutex_;
    std::condition_variable result_cv_;
    bool result_ready_ = false;
};
```

### Step 2: Signal from Physics Thread

**File**: `src/runtime/physics/physics_thread.cc`

When physics results are ready:

```cpp
void PhysicsThread::OnStepComplete(PhysicsResult result) {
    {
        std::lock_guard lock(result_mutex_);
        result_ = std::move(result);
        result_ready_ = true;
    }
    result_cv_.notify_one();
}
```

### Step 3: Wait on Main Thread

**File**: `src/runtime/physics/physics_system.cc` (~line 264)

```cpp
std::optional<PhysicsResult> PhysicsSystem::FetchResult(int timeout_ms) {
    std::unique_lock lock(result_mutex_);
    if (!result_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [this] { return result_ready_; })) {
        /* Timeout — no result available */
        return std::nullopt;
    }

    auto result = std::move(result_);
    result_ready_ = false;
    return result;
}
```

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/physics_fetch_cv_test.cc`:
- FetchResult receives result from physics thread without busy-waiting (verify CPU usage near zero)
- Timeout: no result produced within deadline → FetchResult returns nullopt
- Multiple FetchResult calls without new physics data: second call waits on CV, not spinning
- Signal from physics thread wakes waiting FetchResult immediately (latency < 1ms)

```
src/tests/unit/physics_fetch_cv_test.cc   # ~60 lines
```

## Acceptance Criteria

1. `condition_variable` replaces busy-wait spin loop
2. Physics thread signals `result_cv_` when results are ready
3. Main thread `FetchResult` waits with timeout, returns immediately when signaled
4. CPU usage during FetchResult is near zero (no spinning)
5. Latency from physics ready to main thread receipt < 1ms

## Dependencies

- P1-4 (PhysicsThread CV Wakeup) — same pattern, different direction (physics → main)

## Estimated Effort: ~80 lines
