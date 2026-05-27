# P2-8: evpp Release Build Thread Safety Checks

## Objective

Make the `EventAdd`/`EventDel` thread safety checks (currently `#ifdef H_DEBUG_MODE` only) available in Release builds.

## Current State

`evpp/inner_pre.cc:35-56` implements thread safety checks for libevent `event_add`/`event_del`, but only under `#ifdef H_DEBUG_MODE`. In Release builds, these calls are raw passthroughs with zero thread safety verification.

libevent requires `event_add`/`event_del` on the owning event loop's thread. Cross-thread calls corrupt libevent's internal data structures, causing non-deterministic crashes, lost events, and silent I/O failure.

## Implementation Steps

### Step 1: Add Minimal Release Checks

**File**: `src/runtime/evpp/inner_pre.cc`

```cpp
/* Thread safety check: always active in all build modes */
static thread_local struct event_base* g_current_event_base = nullptr;

int EventAdd(struct event* ev, const struct timeval* timeout) {
#ifdef H_DEBUG_MODE
    /* Full checks: map-lookup for duplicate add, cross-thread del, etc. */
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = evmap.find(ev);
        if (it != evmap.end()) {
            assert(false && "event_add twice");
        }
        evmap[ev] = std::this_thread::get_id();
    }
#else
    /* Release checks: lightweight assertion — no map, no mutex */
    /* Verify we're on the owning event base's thread */
    struct event_base* base = event_get_base(ev);
    if (base && g_current_event_base && base != g_current_event_base) {
        ENGINE_LOG_ERROR("event_add called from wrong thread! "
                         "Expected base=%p, current base=%p",
                         base, g_current_event_base);
        /* Don't abort — but log the violation for post-mortem analysis */
    }
#endif
    return event_add(ev, timeout);
}

/* Also set g_current_event_base at event loop entry points:
 * In EventLoop::Run(): g_current_event_base = base_;
 * In EventLoop::RunInLoop callbacks: thread-local check
 */
```

### Step 2: Document the Check

Add a comment in `inner_pre.cc` explaining the two-tier checking:
- Debug: full `std::map`-based duplicate/cross-thread checking (expensive but thorough)
- Release: lightweight thread-local check (cheap, catches wrong-thread calls)

### Step 3: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/event_thread_safety_test.cc`:
- Verify `EventAdd` rejects (logs ERROR) when called from a non-owning thread in Release mode
- Verify `EventAdd` succeeds when called from the owning thread in Release mode
- Verify `g_current_event_base` is correctly set at event loop entry points (`EventLoop::Run()`, `RunInLoop` callbacks)
- Debug mode: verify the full map-based duplicate-add assertion fires
- Debug mode: verify cross-thread `EventDel` assertion fires

**Fake infrastructure** — `src/tests/fakes/fake_event_loop.h` (extend existing):
- Add `GetEventBase()` method to expose the underlying `event_base*` for verification
- Add `SetOwningThread()` tracking for cross-thread call detection in tests

```
src/tests/unit/event_thread_safety_test.cc   # ~50 lines
src/tests/fakes/fake_event_loop.h            # +15 lines (extend existing fake)
```

## Acceptance Criteria

1. Release builds include a lightweight thread safety check for `event_add`/`event_del`
2. Wrong-thread calls are logged at ERROR level
3. Debug builds retain the full map-based checking
4. Performance impact in Release is negligible (thread_local read + pointer compare)
5. Unit tests verify wrong-thread calls are detected in both Debug and Release modes
6. Unit tests verify correct-thread calls succeed in both modes

## Dependencies: P0-3 (Test Infrastructure, FakeEventLoop) | Estimated Effort: ~100 lines + ~65 lines tests
