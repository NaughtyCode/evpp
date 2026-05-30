# Timer Runtime Hardening - 2026-05-30

## Summary

Deep-audited the timer implementation under `src/runtime/core/timer` from a
threading and callback-lifetime perspective. The timer subsystem is now modeled
as thread-confined: each `TimerManager` binds to one owner thread, and Debug
builds fail fast when public APIs are called from any other thread.

## Fixes

- Added `TimerManager` thread ownership:
  - `initialize()` binds the manager to the current thread automatically.
  - `bind_to_current_thread()` allows explicit pre-initialization binding.
  - Debug builds log and assert on cross-thread public API access.
  - Release builds keep binding metadata queryable without adding runtime
    assertions.
- Added public ownership diagnostics:
  - `has_thread_binding()`
  - `is_bound_to_current_thread()`
- Made callback-time destruction safe:
  - `destroy_timer()` now defers entry deletion while `update()` is dispatching
    callbacks.
  - Deferred-destroy IDs are hidden from later API lookups so destruction stays
    terminal even if a callback tries to restart the same timer ID.
  - Deferred deletion is flushed when the outermost update scope exits.
- Hardened `HrTimerManager::process_expired()` by copying the callback before
  invocation, preventing use-after-free when a callback destroys the timer node
  that owns the `std::function` storage.
- Hardened timer-wheel expiry semantics:
  - Expired batch nodes are marked `kFiring` before callbacks run.
  - A timer cancelled or destroyed by an earlier callback in the same batch is
    skipped instead of firing through a dangling raw pointer.
  - `TimerWheelNode::fire()` no longer overwrites a callback-initiated re-arm or
    cancel state.
- Hardened alarm timer cancellation during callbacks:
  - Alarms now expose `TimerState`-compatible state.
  - Cancelling a firing alarm marks it cancelled rather than silently failing.
- Fixed Lua one-shot timer cleanup:
  - `timer.timeout()` now destroys its underlying `TimerManager` entry after
    the callback fires, avoiding leaked one-shot timer entries.

## Tests

- Added thread-binding metadata coverage for `TimerManager`.
- Added hrtimer self-destruction coverage, including a post-destroy restart
  attempt from inside the callback.
- Added timer-wheel batch destruction coverage where one expired callback
  destroys another expired timer before it can fire.
- Re-ran existing Lua timer once/interval tests against the rebuilt Debug
  runtime.

## Verification

- `cmake --build artifacts\build --config Debug --target test_timer lua_test_runner --parallel`
- `ctest --test-dir artifacts\build -C Debug -R "lua\.timer|unit\.timer" --output-on-failure`
- `cmake --build artifacts\build --config Release --target test_timer --parallel`
- `ctest --test-dir artifacts\build -C Release -R "^unit\.timer$" --output-on-failure`
- `git diff --check`

## Notes

- Running the Release Lua timer CTest directly currently fails before executing
  timer logic because `tests.harness.test_harness` is not found in the Lua import
  path. The same Lua timer tests pass in Debug after rebuilding
  `lua_test_runner`, and the Release C++ timer unit target passes.
