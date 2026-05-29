# Per-Thread TimerManager — DBThread & PhysicsThread

## Summary

DBThread and PhysicsThread now each own a dedicated `TimerManager` instance, with
the `timer.timeout` / `timer.interval` / `timer.cancel` Lua API registered into
their respective script VMs.

## Changes

### DBThread
- `db_thread.h`: Added `std::unique_ptr<TimerManager> timer_mgr_` member and
  `GetTimerManager()` accessor.
- `db_thread.cc`:
  - **Phase 2 (init)**: Creates `timer_mgr_`, initializes it, and calls
    `script::ExportTimer(script_vm_, *timer_mgr_)` — making `timer.*` available
    in DB service Lua scripts.
  - **Phase 3 (main loop)**: Calls `timer_mgr_->update()` each frame, after
    request processing and `CallFrameCallback`.
  - **Phase 4 (cleanup)**: Shuts down and resets `timer_mgr_` before
    `DestroyScript`.

### PhysicsThread
- `physics_thread.h`: Added `InitTimerManager()` method, `GetTimerManager()`
  accessor, and `std::unique_ptr<TimerManager> timer_mgr_` member.
- `physics_thread.cc`:
  - `InitTimerManager()`: Creates and initializes the per-thread instance (MT,
    called before `Start()`).
  - `EventLoop()`: Calls `timer_mgr_->update()` after each `world_.Step()` and
    post-step callback, alongside the Lua collision callback.
  - `~PhysicsThread()`: Shuts down and resets `timer_mgr_` after `Stop()`.
- `physics_system.cc`: In `Initialize()`, calls `physics_thread_.InitTimerManager()`
  then `script::ExportTimer(*script_vm_, physics_thread_.GetTimerManager())`,
  registering the timer API before scripts are loaded.

### Thread Safety
- Each `TimerManager` instance is thread-confined: created on MT, updated
  exclusively on its owning worker thread (DBT / PT), and destroyed on MT
  after the worker thread has been joined.
