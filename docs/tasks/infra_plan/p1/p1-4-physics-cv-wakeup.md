# P1-4: PhysicsThread EventLoop — Replace 50ms Poll-Sleep with condition_variable

## Objective

Replace the 50ms polling sleep in PhysicsThread's event loop with a `std::condition_variable` to eliminate 0-50ms of unnecessary command dispatch latency.

## Current State

`physics_thread.cc:274` uses a polling sleep when the command queue is empty:

```cpp
/* physics_thread.cc — simplified */
void PhysicsThread::EventLoop() {
    while (running_) {
        PhysicsCommand cmd;
        if (command_queue_.try_dequeue(cmd)) {
            ProcessCommand(cmd);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));  /* 50ms poll */
        }
    }
}
```

For a 60fps game (16.67ms/frame), every physics command (Spawn, ApplyForce, Tick) incurs 0-50ms of additional latency. This is **longer than a single frame**, causing perceptible input lag.

## Root Cause

Simple polling-based design. `moodycamel::ConcurrentQueue` was chosen for its lock-free performance, but its `try_dequeue` is non-blocking. Without a blocking wait mechanism, the thread must poll.

## Impact

- Physics commands experience 0-50ms random latency
- At 60fps, this exceeds a single frame time (16.67ms)
- Wastes CPU cycles with unnecessary wake-ups (20/second even when idle)
- Makes physics feel "sluggish" compared to local simulation

## Implementation Steps

### Step 1: Add condition_variable to PhysicsThread

**File**: `src/runtime/physics/physics_thread.h`

```cpp
class PhysicsThread {
    /* ... */
private:
    std::atomic<bool> running_{false};
    moodycamel::ConcurrentQueue<PhysicsCommand> command_queue_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    /* ... */
};
```

### Step 2: Signal on Enqueue

**File**: `src/runtime/physics/physics_thread.cc`

In the enqueue function (wherever commands are pushed):

```cpp
void PhysicsThread::EnqueueCommand(PhysicsCommand cmd) {
    command_queue_.enqueue(std::move(cmd));
    cv_.notify_one();  /* wake up the event loop */
}
```

### Step 3: Replace Poll-Sleep with Timed Wait

**File**: `src/runtime/physics/physics_thread.cc` (~line 274)

```cpp
void PhysicsThread::EventLoop() {
    while (running_) {
        PhysicsCommand cmd;
        if (command_queue_.try_dequeue(cmd)) {
            ProcessCommand(cmd);
            /* Continue processing remaining commands without waiting */
            continue;
        }

        /* Queue empty — wait with timeout as safety net */
        std::unique_lock lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
            return !running_ || command_queue_.size_approx() > 0;
        });
        /* Wakes up either:
         * 1. cv_.notify_one() called from EnqueueCommand
         * 2. 50ms timeout (safety net for spurious wakeup / missed signal)
         * 3. running_ set to false (shutdown) */
    }
}
```

### Step 4: Fix Stop() Wake-Up

**File**: `src/runtime/physics/physics_thread.cc` (~line 131)

Currently `Stop()` sends a fake `Tick(0, 0.0f)` to wake the loop — a hack that could race with a real Tick:

```cpp
/* Before: */
void PhysicsThread::Stop() {
    running_ = false;
    EnqueueCommand(MakeTick(TickArgs{0, 0.0f}));  /* hack: fake tick to wake loop */
    Join();
}

/* After: */
void PhysicsThread::Stop() {
    running_ = false;
    cv_.notify_one();  /* clean wake-up, no fake command needed */
    Join();
}
```

### Step 5: Tests

**File**: `src/tests/unit/physics_thread_test.cc`

```cpp
TEST(PhysicsThreadTest, CommandDispatchedImmediately) {
    PhysicsThread pt;
    pt.Start();

    auto start = std::chrono::steady_clock::now();
    pt.EnqueueCommand(MakeSpawnCommand(...));

    /* Wait for processing — should be near-immediate */
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto latency = std::chrono::steady_clock::now() - start;

    /* Latency should be well under 50ms (the old worst case) */
    EXPECT_LT(latency, std::chrono::milliseconds(10));

    pt.Stop();
}

TEST(PhysicsThreadTest, StopWakesEventLoop) {
    PhysicsThread pt;
    pt.Start();

    auto start = std::chrono::steady_clock::now();
    pt.Stop();
    auto elapsed = std::chrono::steady_clock::now() - start;

    /* Stop should complete quickly, not wait for 50ms poll cycle */
    EXPECT_LT(elapsed, std::chrono::milliseconds(10));
}
```

## Acceptance Criteria

1. `condition_variable` replaces `sleep_for(50ms)` in PhysicsThread event loop
2. `EnqueueCommand` signals the condition variable
3. `Stop()` uses `cv_.notify_one()` instead of a fake Tick command
4. Command dispatch latency < 5ms (down from 0-50ms)
5. Shutdown latency < 10ms (down from up to 50ms)
6. CPU usage at idle is near zero (no polling)
7. Tests verify latency improvement

## Dependencies

- None (independent)

## Estimated Effort

- Header changes: ~5 lines
- EventLoop refactor: ~15 lines
- EnqueueCommand change: ~2 lines
- Stop() change: ~3 lines
- Tests: ~80 lines
- **Total**: ~100 lines

## Risks

- **Missed wake-up**: If `notify_one()` happens before `wait_for()`, the timeout provides a safety net (50ms max delay, same as current). This is acceptable.
- **Spurious wake-up**: `wait_for()` with predicate handles this correctly — it re-checks the condition before returning.
- **`size_approx()` accuracy**: `moodycamel::ConcurrentQueue::size_approx()` is approximate. May wake spuriously, but that's harmless (just does a `try_dequeue` that fails, then goes back to waiting).
