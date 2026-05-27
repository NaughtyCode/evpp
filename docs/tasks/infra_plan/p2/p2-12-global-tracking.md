# P2-12: Global Variable Ownership Tracking + ClearCache Cleanup

## Objective

Track which Lua global variables belong to which module, and ensure `ScriptImporter::ClearCache()` properly cleans them up. This fixes the issue where hot-reloading leaves stale global variables from old script versions.

## Current State

`ScriptImporter::ClearCache()` empties `package.loaded` but does not clear global variables set by loaded modules. After hot-reload:
- Old module's global functions remain accessible
- New module's functions may conflict silently
- No way to know which globals belong to which module

## Implementation Steps

### Step 1: Track Global Variable Ownership

**File**: `src/runtime/vm/script_importer.cc`

```cpp
/* Per-module global variable tracking */
struct ModuleGlobals {
    std::string module_name;
    std::vector<std::string> global_keys;  /* globals set by this module */
};

static std::unordered_map<std::string, ModuleGlobals> g_module_globals;

/* Hook called after each import to snapshot new globals */
void ScriptImporter::TrackModuleGlobals(lua_State* L, const std::string& module_name) {
    ModuleGlobals mg;
    mg.module_name = module_name;

    /* Snapshot globals before import */
    std::unordered_set<std::string> before = SnapshotGlobalKeys(L);

    /* ... import module ... */

    /* Find new globals after import */
    std::unordered_set<std::string> after = SnapshotGlobalKeys(L);
    for (auto& key : after) {
        if (before.find(key) == before.end()) {
            mg.global_keys.push_back(key);
        }
    }

    g_module_globals[module_name] = std::move(mg);
}
```

### Step 2: Cleanup on ClearCache

```cpp
void ScriptImporter::ClearCache() {
    auto L = Engine::Instance().GetScriptVM().GetState();

    /* Clear tracked globals */
    for (auto& [name, mg] : g_module_globals) {
        for (auto& key : mg.global_keys) {
            lua_pushnil(L);
            lua_setglobal(L, key.c_str());
        }
    }
    g_module_globals.clear();

    /* Clear package.loaded */
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_pushvalue(L, -2);
        lua_pushnil(L);
        lua_settable(L, -4);
        lua_pop(L, 1);
    }
    lua_pop(L, 2);  /* package.loaded, package */

    /* Re-register engine globals (ExportAll) */
    ExportAll(Engine::Instance().GetScriptVM());
}
```

### Step 3: Lua Module Convention

Encourage modules to return a local table instead of setting globals:

```lua
-- Preferred (tracked per module):
local M = {}
function M.do_something() end
return M

-- Discouraged (global pollution):
function do_something() end
```

Add a warning when a module sets globals: `log_warn("Module '{}' sets global '{}' — prefer returning a local table", module, key)`

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/global_tracking_test.cc`:
- Import module that sets globals → verify ownership is tracked in `g_module_globals`
- ClearCache: verify all tracked globals are nil'd after clearing
- Re-import after ClearCache: verify new globals are set correctly with fresh tracking
- Module that returns local table → verify no global tracking entry needed
- Warning logged when module sets globals (verify via log capture with opt-in config)

```
src/tests/unit/global_tracking_test.cc   # ~60 lines
```

## Acceptance Criteria

1. Module global variable ownership is tracked in `g_module_globals`
2. `ClearCache()` nils all tracked globals before clearing `package.loaded`
3. `ExportAll()` is re-invoked after ClearCache to restore engine globals
4. Warning is logged when modules set globals (opt-in via config)
5. Tests verify global tracking and cleanup

## Dependencies

- P1-8 (Hot-Reload System) — ClearCache is the core mechanism for hot-reload

## Estimated Effort: ~200 lines
