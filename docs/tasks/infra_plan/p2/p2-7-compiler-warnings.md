# P2-7: Compiler Warning Configuration Unification (UNIX/MSVC)

## Objective

Unify compiler warning configurations so that Windows builds are not a "zero warning" mode that hides bugs caught on Linux.

## Current State

- **UNIX**: `-Wall -Wextra -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings -Wsign-compare -Wfloat-equal`
- **MSVC**: 12 `/wd` flags disabling nearly all common warnings

A bug that produces warnings on Linux is completely silent on Windows.

## Implementation Steps

### Step 1: Replace /wd with /w1 + Explicit Disables

**File**: `CMakeLists.txt`

```cmake
if(MSVC)
    # Enable warning level 4 (equivalent to -Wall -Wextra)
    target_compile_options(evpp_runtime PRIVATE /W4)

    # Disable only warnings that are genuinely noise:
    # C4100: unreferenced formal parameter (common in callback stubs)
    # C4127: conditional expression is constant (common in templates)
    # C4201: nonstandard extension: nameless struct/union (used in evpp)
    target_compile_options(evpp_runtime PRIVATE /wd4100 /wd4127 /wd4201)

    # Treat warnings as errors:
    target_compile_options(evpp_runtime PRIVATE /WX)
else()
    # Existing UNIX flags + -Werror
    target_compile_options(evpp_runtime PRIVATE -Werror)
endif()
```

### Step 2: Fix All New Warnings

Run the build with the new warning flags on both platforms. Fix all warnings:
- Signed/unsigned comparison mismatches
- Unused variables
- Uninitialized variables
- Potentially uninitialized variables
- Implicit conversions

### Step 3: Add to CI

CI pipelines check both Linux and Windows builds with -Werror / /WX.

## Acceptance Criteria

1. Windows builds use /W4 (not /wd-everything)
2. Only 3 genuinely necessary warning suppressions remain
3. Both platforms use "warnings as errors"
4. Zero warnings on both platforms
5. CI enforces zero-warning policy

## Dependencies: None | Estimated Effort: ~50 lines CMake + warning fixes
