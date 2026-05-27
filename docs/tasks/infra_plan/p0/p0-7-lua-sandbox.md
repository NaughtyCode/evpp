# P0-7: Lua Sandbox — Replace luaL_openlibs with Whitelist-Based Loading

## Objective

Replace the unsafe `luaL_openlibs(L_)` call in ScriptVM with a whitelist-based library loader. Block or sanitize dangerous standard library functions (`os.execute`, `io.open`, `debug.*`, `package.loadlib`). Provide configurable security levels.

## Current State

`ScriptVM` constructor (`vm.cc:23`) loads ALL Lua standard libraries:

```cpp
luaL_openlibs(L_);  /* loads: basic, coroutine, table, io, os, string, math, utf8, debug, package */
```

Any Lua script can:
| Capability | Function | Risk |
|-----------|----------|------|
| Execute system commands | `os.execute("rm -rf /")` | Full OS compromise |
| Read/write any file | `io.open("/etc/passwd")` | Data exfiltration |
| Delete files | `os.remove()`, `os.rename()` | Data destruction |
| Read environment variables | `os.getenv("DB_PASSWORD")` | Credential theft |
| Terminate process | `os.exit()` | Denial of service |
| Access debug internals | `debug.getregistry()`, `debug.getupvalue()` | Sandbox escape |
| Load native C libraries | `package.loadlib()` | Arbitrary code execution |

Even without executing client-sent scripts, this means:
- Any third-party Lua module can execute system commands
- A Lua bug (infinite recursion + `os.execute`) can cripple the OS
- Compromised config file paths (`entry_scripts_dir`) grant shell access

## Root Cause

"Convenience first" design — `luaL_openlibs` is a one-liner that loads everything. No security review was done for a server deployment context.

## Impact

For server infrastructure, this means anyone who can write Lua script files or trigger `DoString` execution gains OS-level access equal to the server process. In multi-tenant deployments, one tenant's scripts can read other tenants' data.

## Implementation Steps

### Step 1: Define Security Levels

**File**: `src/runtime/config/runtime_config.h`

```cpp
enum class LuaSandboxLevel {
    Strict,   /* Production: no io, no os.execute/exit/remove/rename, no debug, no package.loadlib */
    Server,   /* Trusted server: os.* allowed but debug/package removed (for dev/staging) */
    Full      /* Development: all libraries (current behavior) */
};
```

### Step 2: Implement Whitelist Loader

**File**: `src/runtime/vm/sandbox.h`

```cpp
/*
 * Sandboxed replacement for luaL_openlibs.
 * Loads only whitelisted libraries based on the configured security level.
 */
void luaL_openlibs_sandboxed(lua_State* L, LuaSandboxLevel level);
```

**File**: `src/runtime/vm/sandbox.cc`

```cpp
void luaL_openlibs_sandboxed(lua_State* L, LuaSandboxLevel level) {
    /* Always safe — pure computation, no OS access */
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);   lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);  lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);   lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);   lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);

    /* package — needed for require/import, but remove loadlib */
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
    /* Remove dangerous package.loadlib */
    lua_getglobal(L, "package");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadlib");
    lua_pop(L, 1);

    if (level == LuaSandboxLevel::Full) {
        /* Development: load everything */
        luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);     lua_pop(L, 1);
        luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);     lua_pop(L, 1);
        luaL_requiref(L, LUA_DBLIBNAME, luaopen_debug, 1);  lua_pop(L, 1);
    } else if (level == LuaSandboxLevel::Server) {
        /* Server: io and os allowed, but sanitize dangerous os functions */
        luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);     lua_pop(L, 1);
        luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);     lua_pop(L, 1);
        /* Remove debug entirely */
        /* package.loadlib already removed above */
    } else {
        /* Strict: no io, no debug, sanitized os */
        luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 1);     lua_pop(L, 1);
        /* Remove dangerous os functions */
        lua_getglobal(L, "os");
        lua_pushnil(L); lua_setfield(L, -2, "execute");
        lua_pushnil(L); lua_setfield(L, -2, "exit");
        lua_pushnil(L); lua_setfield(L, -2, "remove");
        lua_pushnil(L); lua_setfield(L, -2, "rename");
        lua_pushnil(L); lua_setfield(L, -2, "getenv");
        lua_pop(L, 1);
    }

    /* basic library — always loaded (contains print, error, pcall, etc.) */
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);  lua_pop(L, 1);
}
```

### Step 3: Update ScriptVM Constructor

**File**: `src/runtime/vm/vm.cc` (line ~23)

```cpp
/* Before: */
luaL_openlibs(L_);

/* After: */
luaL_openlibs_sandboxed(L_, config_.sandbox_level);
```

### Step 4: Add Config

**File**: `resources/config/public_config.json`

```json
{
  "script": {
    "sandbox_level": "strict"
  }
}
```

**File**: `resources/config/dev_config.json`

```json
{
  "script": {
    "sandbox_level": "full"
  }
}
```

### Step 5: Add RuntimeConfig Field

**File**: `src/runtime/config/runtime_config.h`

```cpp
struct RuntimeConfig {
    /* ... existing fields ... */
    LuaSandboxLevel sandbox_level = LuaSandboxLevel::Strict;
};
```

### Step 6: Audit Existing Lua Scripts for Compatibility

Check all Lua scripts in `resources/script/`:
- `server.lua` — no dangerous calls expected
- `client.lua` — no dangerous calls expected
- `class.lua` — no dangerous calls expected
- Test scripts — only need `os.exit()` for test runner (use `full` level for tests)

### Step 7: Add Audit Logging for Sensitive Operations

**File**: `src/runtime/vm/sandbox.cc`

Wrap remaining `os` functions with audit logging:

```cpp
/* For Server level: wrap os.clock/os.time/os.date with audit log */
/* This is optional — only if compliance/audit requirements exist */
```

### Step 8: Tests

**File**: `src/tests/unit/sandbox_test.cc`

```cpp
TEST(SandboxTest, StrictLevel_NoOsExecute) {
    ScriptVM vm(LuaSandboxLevel::Strict);
    /* os.execute should be nil */
    bool ok = vm.DoString("assert(os.execute == nil)");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, StrictLevel_NoIo) {
    ScriptVM vm(LuaSandboxLevel::Strict);
    bool ok = vm.DoString("assert(io == nil)");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, StrictLevel_NoDebug) {
    ScriptVM vm(LuaSandboxLevel::Strict);
    bool ok = vm.DoString("assert(debug == nil)");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, StrictLevel_HasTableStringMath) {
    ScriptVM vm(LuaSandboxLevel::Strict);
    bool ok = vm.DoString(R"(
        assert(type(table.sort) == "function")
        assert(type(string.format) == "function")
        assert(type(math.abs) == "function")
    )");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, ServerLevel_HasOsClock_NoOsExecute) {
    ScriptVM vm(LuaSandboxLevel::Server);
    bool ok = vm.DoString(R"(
        assert(type(os.clock) == "function")
        assert(os.execute == nil)
    )");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, FullLevel_AllLibrariesAvailable) {
    ScriptVM vm(LuaSandboxLevel::Full);
    bool ok = vm.DoString(R"(
        assert(type(os.execute) == "function")
        assert(type(io.open) == "function")
    )");
    EXPECT_TRUE(ok);
}

TEST(SandboxTest, NoPackageLoadlib) {
    ScriptVM vm(LuaSandboxLevel::Full);  /* even full removes loadlib */
    bool ok = vm.DoString("assert(package.loadlib == nil)");
    EXPECT_TRUE(ok);
}
```

## Acceptance Criteria

1. `Strict` level: no `os.execute`, `os.exit`, `os.remove`, `os.rename`, `os.getenv`, `io.*`, `debug.*`, `package.loadlib`
2. `Server` level: `os.*` allowed, `io.*` allowed, `package.loadlib` removed, `debug.*` removed
3. `Full` level: all libraries available, only `package.loadlib` removed
4. `table`, `string`, `math`, `coroutine`, `utf8` always available
5. Sandbox level configurable via JSON config
6. Default is `strict` for public/production config
7. All existing Lua scripts work under appropriate sandbox level
8. Tests verify each level's permissions

## Dependencies

- None (independent change)

## Estimated Effort

- `sandbox.h` + `sandbox.cc`: ~80 lines
- `vm.cc` change: ~5 lines
- Config changes: ~10 lines
- Tests: ~100 lines
- **Total**: ~200 lines

## Risks

- **Backward compatibility**: Existing Lua scripts may use `os.clock()` or `os.date()` for profiling. Mitigation: `os.clock()` and `os.date()` are safe and can be kept even in Strict mode. The plan above keeps the entire `os` table in Strict but removes only the genuinely dangerous functions.
- **`io` library**: Some Lua modules may use `io.open` for reading data files. In Strict mode, provide a sandboxed file API that only allows reading from whitelisted directories. This is a P2 enhancement — for P0, simply remove `io` in Strict mode.
- **`require` dependence**: Removing `package.loadlib` is safe because the engine uses `ScriptImporter` for module loading, not `require` with C libraries.
