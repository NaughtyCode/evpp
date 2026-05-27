# P3-8: Engine::Cleanup Lifecycle Order Documentation + Assertions

## Objective

Document the implicit lifecycle ordering in `Engine::Cleanup()` and add assertions to detect ordering violations at runtime.

## Current State

`Engine::Cleanup()` has 13 order-sensitive steps. The dependency chain is implicit:

```cpp
/* engine.cc:344 — Physics Shutdown (destroy physics VM + physics thread) */
/* engine.cc:348 — DB Shutdown (stop DB threads + connection pool) */
/* engine.cc:359 — ShutdownNetBindings (needs script_vm_ Lua state alive) */
/* engine.cc:362 — ShutdownTimerBindings (needs script_vm_) */
/* engine.cc:366-370 — DestroyScript → lua_gc query → final log */
```

`ShutdownNetBindings` internally calls `Engine::Instance().GetScriptVM().GetState()` to unref Lua objects — it assumes `script_vm_` is still alive. If someone reorders Cleanup steps, this becomes a null pointer dereference with no diagnostic.

## Implementation Steps

### Step 1: Add Lifecycle State Assertions

**File**: `src/runtime/engine/engine.cc`

```cpp
/* Document the lifecycle phases and assert correct ordering */
enum class CleanupPhase {
    NotStarted,
    PhysicsShutdown,
    DatabaseShutdown,
    NetworkShutdown,      /* script_vm_ still alive */
    TimerShutdown,        /* script_vm_ still alive */
    ScriptDestroyed,      /* script_vm_ destroyed */
    FinalLogs,
    Complete
};

void Engine::Cleanup() {
    cleanup_phase_ = CleanupPhase::PhysicsShutdown;
    PhysicsEngineBridge::Instance().Shutdown();

    cleanup_phase_ = CleanupPhase::DatabaseShutdown;
    DatabaseService::Instance().Shutdown();

    cleanup_phase_ = CleanupPhase::NetworkShutdown;
    /* ASSERT: script_vm_ is alive */
    assert(script_vm_ != nullptr);
    ShutdownNetBindings();

    cleanup_phase_ = CleanupPhase::TimerShutdown;
    assert(script_vm_ != nullptr);
    ShutdownTimerBindings();

    cleanup_phase_ = CleanupPhase::ScriptDestroyed;
    DestroyScript();
    script_vm_.reset();

    cleanup_phase_ = CleanupPhase::FinalLogs;
    /* ... */

    cleanup_phase_ = CleanupPhase::Complete;
}
```

### Step 2: Add Diagnostic in Critical Functions

```cpp
ScriptVM& Engine::GetScriptVM() {
    if (!script_vm_) {
        ENGINE_LOG_CRITICAL(
            "GetScriptVM() called but ScriptVM is null. "
            "Cleanup phase: {}. This is a lifecycle ordering bug.",
            static_cast<int>(cleanup_phase_));
        /* Fast fail in debug, log+throw in release */
    }
    return *script_vm_;
}
```

### Step 3: Document in Header

**File**: `src/runtime/engine/engine.h`

```cpp
/*
 * Cleanup Lifecycle Order (MUST be maintained):
 *
 *   1. Physics Shutdown         — stops physics VM + physics thread
 *   2. Database Shutdown        — stops DB threads + drains queues
 *   3. Network Shutdown         — [REQUIRES script_vm_ alive]
 *   4. Timer Shutdown           — [REQUIRES script_vm_ alive]
 *   5. DestroyScript            — Destroys Lua state (InitScript/UpdateScript/DestroyScript hooks)
 *   6. Final Logs               — GC stats, cleanup confirmation
 *
 * CRITICAL: Steps 3 and 4 require script_vm_ to be alive.
 * Do NOT reorder without updating ALL callers.
 */
```

## Acceptance Criteria

1. `CleanupPhase` enum tracks cleanup progress
2. Assertions verify `script_vm_` is alive when accessed by ShutdownBindings
3. Ordering violation produces a clear diagnostic message
4. Cleanup order is documented in `engine.h`
5. Tests: intentionally reorder cleanup, verify assertion fires

## Dependencies: None | Estimated Effort: ~30 lines
