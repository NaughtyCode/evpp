# P1-8: Hot-Reload System — File Watch + Validate + Rollback

## Objective

Implement a complete hot-reload system: file change monitoring, change detection, script validation before applying, and rollback on failure. Builds on existing infrastructure (`class.lua` weak-reference registry, `ScriptImporter::ClearCache()`).

## Current State

- `class.lua` has weak-reference registry supporting class hot-reload — but reload is triggered manually
- `ScriptImporter::ClearCache()` clears `package.loaded` — but old global variables persist
- `ConfigManager::Reload()` has reload logic — but no file watching
- **No file monitoring, change detection, validation, or rollback exists**

## Root Cause

Hot-reload was partially designed (the Lua-side class system supports it) but the C++ side was never wired up to filesystem events or a validation/rollback workflow.

## Implementation Steps

### Step 1: Add File Watcher

**File**: `src/runtime/vm/file_watcher.h`

```cpp
/*
 * Cross-platform file watcher for detecting script changes.
 *
 * On Linux: uses inotify.
 * On macOS: uses kqueue/FSEvents.
 * On Windows: uses ReadDirectoryChangesW.
 *
 * Callbacks are invoked on the watcher's own thread.
 * Use RunInLoop to dispatch to the main thread for Lua state access.
 */
class FileWatcher {
public:
    /* Watch a directory recursively for .lua file changes */
    void WatchDirectory(const std::string& path);

    /* Set callback: invoked with list of changed file paths */
    using ChangeCallback = std::function<void(const std::vector<std::string>& changed_files)>;
    void SetChangeCallback(ChangeCallback callback);

    void Start();
    void Stop();

private:
    /* Platform-specific implementation */
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

### Step 2: Implement Change Detection with Debounce

**File**: `src/runtime/vm/script_reloader.h`

```cpp
/*
 * ScriptReloader manages the hot-reload lifecycle:
 *   1. Detect file changes (debounced — batch rapid saves)
 *   2. Validate new scripts (load in a sandbox VM)
 *   3. Apply reload (ClearCache + re-import)
 *   4. Rollback on failure (restore previous cache)
 */
class ScriptReloader {
public:
    void SetScriptDirectories(const std::vector<std::string>& dirs);
    void Start();
    void Stop();

    /* Manual reload trigger (for API/CLI use) */
    bool ReloadFile(const std::string& filepath);
    bool ReloadAll();

    /* Callbacks for monitoring */
    using ReloadCallback = std::function<void(const std::string& filepath, bool success)>;
    void SetReloadCallback(ReloadCallback callback);

private:
    /* Validate a script file in an isolated Lua VM */
    bool ValidateScript(const std::string& filepath);

    /* Snapshot current global state for rollback */
    void SnapshotGlobals(lua_State* L);
    void RestoreGlobals(lua_State* L);

    std::unique_ptr<FileWatcher> watcher_;
    std::vector<std::string> script_dirs_;
    int debounce_ms_ = 300;  /* batch changes within 300ms window */

    /* Rollback state */
    std::unordered_map<std::string, std::string> global_snapshot_;
};
```

### Step 3: Implement Global Variable Tracking and Cleanup

**File**: `src/runtime/vm/script_reloader.cc`

```cpp
void ScriptReloader::SnapshotGlobals(lua_State* L) {
    global_snapshot_.clear();

    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        /* key at -2, value at -1 */
        const char* key = lua_tostring(L, -2);
        if (key) {
            /* Deep-copy the value (for basic types) */
            /* Store copy in snapshot */
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  /* global table */
}

bool ScriptReloader::ReloadAll() {
    /* Snapshot globals before reload */
    auto L = Engine::Instance().GetScriptVM().GetState();
    SnapshotGlobals(L);

    /* Clear require cache */
    ScriptImporter::ClearCache();

    /* Re-import all entry scripts */
    bool all_ok = true;
    for (auto& entry : entry_scripts_) {
        if (!ScriptImporter::ImportSingle(L, entry)) {
            all_ok = false;
            break;
        }
    }

    if (!all_ok) {
        /* Rollback: restore globals, re-import previous scripts */
        RestoreGlobals(L);
        ENGINE_LOG_ERROR("Hot-reload failed — rolled back to previous state");
        return false;
    }

    ENGINE_LOG_INFO("Hot-reload successful — {} modules reloaded", entry_scripts_.size());
    return true;
}
```

### Step 4: Wire FileWatcher to ScriptReloader

**File**: `src/runtime/engine/engine.cc`

```cpp
/* In Engine::Init() */
script_reloader_ = std::make_unique<ScriptReloader>();
script_reloader_->SetScriptDirectories(config_.entry_scripts_dir);
script_reloader_->SetReloadCallback([](const std::string& file, bool success) {
    if (success) {
        ENGINE_LOG_INFO("Hot-reloaded: {}", file);
    } else {
        ENGINE_LOG_ERROR("Hot-reload FAILED: {} — rolled back", file);
    }
});

/* In Engine::Start() */
script_reloader_->Start();

/* In Engine::Cleanup() */
script_reloader_->Stop();
```

### Step 5: Add Lua API

**File**: `src/runtime/script/reload_bind.cc`

```cpp
/* hotreload.reload() — trigger manual reload */
/* hotreload.reload_file(path) — reload specific file */
/* hotreload.on_reload(callback) — register reload notification callback */
```

### Step 6: Tests

**File**: `src/tests/unit/script_reloader_test.cc`

- Reload a changed script, verify new code runs
- Reload with syntax error, verify rollback to old state
- Debounce: rapidly write 10 times, verify only one reload
- Global variable preservation across reload

## Acceptance Criteria

1. File changes to `.lua` scripts are detected and trigger hot-reload
2. Changed scripts are validated in an isolated VM before applying
3. Validation failure triggers rollback to previous state
4. `ClearCache` properly cleans up `package.loaded` and tracked globals
5. Hot-reload can be triggered via Lua API or file watch
6. Debounce prevents rapid-fire reloads during bulk save
7. Tests verify reload, validation, and rollback

## Dependencies

- P1-2 (Config Hot-Reload) — consistent notification mechanism
- P2-12 (Global Variable Tracking) — enhances globals snapshot accuracy

## Estimated Effort

- FileWatcher: ~100 lines (header) + platform impls (~150 lines total)
- ScriptReloader: ~100 lines
- Engine integration: ~20 lines
- Lua binding: ~30 lines
- Tests: ~100 lines
- **Total**: ~500 lines

## Risks

- **Platform-specific file watching**: inotify (Linux), ReadDirectoryChangesW (Windows), kqueue (macOS) all have different APIs and edge cases. A cross-platform library (e.g., efsw) could reduce this risk.
- **Reload of running coroutines**: If a coroutine is suspended in a function that's being reloaded, its behavior after the reload is undefined. Mitigation: cancel all coroutines before reload (or document the limitation).
- **Debounce vs latency**: 300ms debounce delays feedback during development. Make debounce configurable (0 for instant reload during dev, 300ms+ for production).
