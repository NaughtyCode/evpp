# P2-18: fprintf Cleanup — Unify All Diagnostics to Quill Logging

## Objective

Replace all 76+ `fprintf(stderr, ...)` calls and ~15 `std::cout` calls with Quill logging macros, consolidating the three separate diagnostic channels into one.

## Current State

Three independent diagnostic channels exist:

| Channel | Location | Count | Problem |
|---------|----------|-------|---------|
| Quill log | `ENGINE_LOG_*` macros | primary | Correct system — timestamps, levels, rotation |
| fprintf(stderr) | 13 files (4 DB layer) | 70 | No timestamps, no levels, bypasses Quill |
| std::cout | `timer_manager.cc:664-682` | 16 | Third channel — only TimerManager DumpStats |

Output from three channels is interleaved in terminal with no unified format. Debugging requires correlating three separate output sources.

## Root Cause

Some subsystems predate the Quill integration or were developed independently. `fprintf` is a C habit that hasn't been cleaned up.

## Implementation Steps

### Step 1: Audit All fprintf and cout Calls

```bash
grep -rn "fprintf(stderr" src/runtime/ --include="*.cc" --include="*.h"
grep -rn "std::cout" src/runtime/ --include="*.cc" --include="*.h"
```

Catalog each call site by:
- File and line number
- Severity (does it represent an error, warning, debug info?)
- Context (can we map it to an existing Quill log level?)

### Step 2: Replace fprintf with ENGINE_LOG_*

For each `fprintf(stderr, ...)` call:

```cpp
/* Before: */
fprintf(stderr, "DB initialization failed: %s\n", error.message);

/* After: */
ENGINE_LOG_ERROR("Database initialization failed: {}", error.message);

/* Before: */
fprintf(stderr, "Server started on port %d\n", port);

/* After: */
ENGINE_LOG_INFO("Server started on port {}", port);
```

### Step 3: Replace cout with ENGINE_LOG_*

**File**: `src/runtime/timer/timer_manager.cc` (~line 664-682)

```cpp
/* Before: */
void TimerManager::DumpStats() {
    std::cout << "=== Timer Stats ===" << std::endl;
    std::cout << "Active: " << active_count_ << std::endl;
    /* ... */
}

/* After: */
void TimerManager::DumpStats() {
    ENGINE_LOG_INFO("=== Timer Stats ===");
    ENGINE_LOG_INFO("Active: {}", active_count_);
    /* ... */
}
```

### Step 4: Remove fprintf Includes

After replacement, remove `#include <cstdio>` from files that no longer use fprintf (unless needed for other functions).

### Step 5: Add Linter Rule

Add a CI lint check that rejects new `fprintf(stderr` and `std::cout` usage:

```bash
# .github/workflows/ci.yml — lint step
- name: Check for fprintf
  run: |
    if grep -rn "fprintf(stderr" src/ --include="*.cc"; then
      echo "ERROR: fprintf(stderr) found — use ENGINE_LOG_* macros instead"
      exit 1
    fi
```

### Step 6: Tests

After completing each step, add automated tests in the following categorized locations:

**CI enforcement** — `.github/workflows/lint.yml` (already configured):
- CI step fails if `fprintf(stderr` found in `src/runtime/`
- CI step fails if `std::cout` found in `src/runtime/`

**Unit tests** — `src/tests/unit/log_output_test.cc`:
- Verify all former fprintf locations now produce output via Quill (log capture)
- Verify DumpStats output is accessible via log capture
- Verify log levels are correct: status=INFO, recoverable=WARN, failure=ERROR

```
src/tests/unit/log_output_test.cc   # ~40 lines
```

## Acceptance Criteria

1. Zero `fprintf(stderr, ...)` calls in `src/runtime/`
2. Zero `std::cout` calls in `src/runtime/`
3. All diagnostic output goes through Quill logging
4. Log levels are correctly assigned (info for status, warn for recoverable, error for failures)
5. CI lint rejects new fprintf/cout usage
6. All existing tests pass (some may check stderr/stdout — update if needed)

## Dependencies: None | Estimated Effort: ~100 lines (mostly find-and-replace)
