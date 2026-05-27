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

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/compiler_warning_regression_test.cc`:
- Verify that each `/wd` → `/W4` conversion does not re-enable warnings that were suppressed for valid reasons (noise from third-party headers, intentional template patterns)
- Compile a minimal source file with the new flags and verify zero warnings
- Test that `/WX` (or `-Werror`) causes build failure when a warning is present

**Integration tests** — `src/tests/integration/build_config_test.cc`:
- Reuse CMake test infrastructure to configure a build with the new warning flags, compile a test target, and verify exit code 0

**CI enforcement** — `.github/workflows/ci.yml` (already configured):
- Both Linux (`-Werror`) and Windows (`/WX`) builds must pass with zero warnings
- CI failure on any new warning is blocking

```
src/tests/unit/compiler_warning_regression_test.cc   # ~30 lines
src/tests/integration/build_config_test.cc            # ~40 lines (shared with other build-config tests)
```

## Acceptance Criteria

1. Windows builds use /W4 (not /wd-everything)
2. Only 3 genuinely necessary warning suppressions remain
3. Both platforms use "warnings as errors"
4. Zero warnings on both platforms
5. CI enforces zero-warning policy
6. Unit tests verify the warning configuration catches real issues
7. Integration test verifies build succeeds with warning flags

## Dependencies: P0-3 (Test Infrastructure) | Estimated Effort: ~50 lines CMake + warning fixes + ~70 lines tests
