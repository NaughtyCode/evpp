# PhysicsThread Recover — sleep_for polling to condition_variable

**Date**: 2026-05-29
**Scope**: `src/runtime/physics/physics_thread.{h,cc}`

## Summary

Eliminated two `std::this_thread::sleep_for()` calls in `PhysicsThread::Recover()`,
replacing the startup-healthy polling loop with a dedicated `condition_variable`.

## Changes

### 1. Removed unnecessary 100ms sleep after Stop()

`Stop()` already calls `Thread::join()`, which blocks until the physics thread exits.
The 100ms "brief pause to ensure clean shutdown" was a no-op on an already-joined
thread. Removed entirely.

### 2. Replaced 50×100ms polling loop with condition_variable wait

Before:
```cpp
int wait_attempts = 0;
while (!healthy_.load(std::memory_order_acquire) && wait_attempts < 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ++wait_attempts;
}
```

After:
```cpp
std::unique_lock<std::mutex> lock(health_cv_mutex_);
bool became_healthy = health_cv_.wait_for(
    lock, std::chrono::milliseconds(5000),
    [this]() { return healthy_.load(std::memory_order_acquire); });
```

### 3. Added health_cv_ notification in EventLoop

`EventLoop()` now calls `health_cv_.notify_one()` immediately after setting
`healthy_ = true`, waking any waiter blocked in `Recover()`.

## Impact

- **Latency**: Recover now proceeds as soon as the world is healthy (typically
  <50ms) instead of always waiting at least 100ms per poll cycle.
- **Worst case**: Timeout unchanged (5000ms for CV vs 50×100ms=5000ms polling).
- **New members** in `PhysicsThread`: `std::mutex health_cv_mutex_` +
  `std::condition_variable health_cv_`.
