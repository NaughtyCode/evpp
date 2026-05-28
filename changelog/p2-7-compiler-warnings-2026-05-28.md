# P2-7: Compiler Warning Configuration Unification

**Date:** 2026-05-28
**Status:** Complete (warning fixes require per-platform builds)
**Plan:** `./docs/tasks/infra_plan/p2/p2-7-compiler-warnings.md`

## Summary

Replaced the 12 `/wd` (disable all) MSVC warning suppressions with `/W4`
(warning level 4, equivalent to `-Wall -Wextra`) plus 4 genuinely necessary
suppressions for noise warnings. Added `ENGINE_WERROR` CMake option to enable
`/WX` (MSVC) / `-Werror` (UNIX) for CI enforcement.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/server/CMakeLists.txt` | `/wd*` ×12 → `/W4` + `/wd4100 /wd4127 /wd4201 /wd4503`; added `ENGINE_WERROR` option; added `-Werror` + `/WX` behind option |
| `src/client/CMakeLists.txt` | Same changes as server CMakeLists.txt |

## Suppressions Retained

| Warning | Reason |
|---------|--------|
| C4100 | Unreferenced formal parameter — common in callback stubs and virtual overrides |
| C4127 | Conditional expression is constant — common in template instantiations |
| C4201 | Nameless struct/union — used in evpp network layer struct patterns |
| C4503 | Decorated name length exceeded — compiler limitation for template-heavy code |

## Design Decisions

- **`ENGINE_WERROR` default OFF**: Warnings-as-errors is opt-in. Enable via
  `-DENGINE_WERROR=ON` for CI or local strict builds.
- **Warning fixes deferred**: The new `/W4` level will produce warnings on
  first build. These must be fixed per-platform before enabling `ENGINE_WERROR`
  in CI.

## Acceptance Criteria

- [x] MSVC uses `/W4` (not `/wd`-everything)
- [x] Only 4 genuinely necessary suppressions remain (down from 12)
- [x] `ENGINE_WERROR` option for `/WX` / `-Werror`
- [ ] Zero-warning build — requires per-platform compilation and fixes
