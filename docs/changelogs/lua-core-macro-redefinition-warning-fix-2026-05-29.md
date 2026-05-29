# Lua Core Macro Redefinition Warning Fix — remove redundant compile definitions from CloudEngine

**Date**: 2026-05-29
**Scope**: `src/server/CMakeLists.txt` (1 line)

## Summary

Fixed MSVC C4005 warnings ("macro redefinition") for `LUA_CORE` and `LUA_LIB`
when compiling Lua source files as part of the CloudEngine shared library.
The root cause was redundant command-line macro definitions that duplicated
`#define` statements already present in every Lua `.c` file.

## Root Cause

Each Lua source file self-identifies its compilation domain via a `#define`
on line 8:
- Core VM files (`lapi.c`, `ldo.c`, `lgc.c`, …): `#define LUA_CORE`
- Standard library files (`lauxlib.c`, `lbaselib.c`, …): `#define LUA_LIB`

CloudEngine's `target_compile_definitions` on line 251 additionally passed
`/D LUA_CORE` and `/D LUA_LIB` to the compiler, causing MSVC to see the
macro defined twice — once from the command line and once from the source —
triggering C4005 on every Lua translation unit.

GameClient compiled the same Lua sources without these redundant defines and
had zero warnings — confirming the fix direction.

## Fix

Removed `LUA_CORE` and `LUA_LIB` from CloudEngine's compile definitions:

```diff
- target_compile_definitions(CloudEngine PRIVATE ENGINE_BUILD LUA_BUILD_AS_DLL LUA_CORE LUA_LIB)
+ target_compile_definitions(CloudEngine PRIVATE ENGINE_BUILD LUA_BUILD_AS_DLL)
```

`LUA_BUILD_AS_DLL` is retained — it must be defined before the `#include`
chain to control `LUA_API` visibility (`__declspec(dllexport)`) in `luaconf.h`,
and is not self-defined in any Lua source file.

## Impact

- Eliminates 22 C4005 warnings from the server build
- No behavioral change — macro values are identical in both definition paths
- Consistent with GameClient target which already omitted these redundant macros
