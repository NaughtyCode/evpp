# P3-13: Remaining Minor Defects — Catch-All

## Objective

Address the remaining defects from the deficiency analysis that are too small for standalone plans but still represent correctness or robustness issues.

## Items

### 13.1: conn:Send() Return Value Unchecked

**Current State**: `tcp_conn.cc` and binding files call `conn->Send()` without checking the return value. If the send buffer is full or the connection is closed, data is silently dropped.

**Fix** (estimated 30 lines):
- `tcp_conn.cc`: Check return value of `bufferevent_write()` in `Send()` / `SendStringInLoop()`. Log WARN on failure.
- All bind files: Return success/failure status to Lua so scripts can handle send failures.

```cpp
/* In TCPConn::Send(): */
int result = bufferevent_write(bev_, data, len);
if (result != 0) {
    ENGINE_LOG_WARN("Send failed on connection {}: buffer full or connection closed", conn_id_);
}
return result == 0;
```

### 13.2: Circular Dependency Detection in ScriptImporter

**Current State**: `ScriptImporter::ImportSingle` only checks `package.loaded` cache after loading. A circular `import("B")` where B imports A causes infinite recursion until stack overflow.

**Fix** (estimated 40 lines):
- **File**: `src/runtime/vm/script_importer.cc`
- Add an `importing_` set that tracks modules currently being loaded:

```cpp
bool ScriptImporter::ImportSingle(lua_State* L, const std::string& module_name) {
    if (importing_.count(module_name)) {
        ENGINE_LOG_ERROR("Circular dependency detected: module '{}' is already being imported. "
                         "Import stack: {}", module_name, FormatImportStack());
        return false;
    }
    importing_.insert(module_name);
    /* ... load module ... */
    importing_.erase(module_name);
    return true;
}
```

### 13.3: Config Schema Validation

**Current State**: `ConfigManager` uses glaze for JSON parsing, which only checks JSON syntax — not field ranges, types, or required fields. A missing required field or invalid range silently produces default values, causing subtle runtime misbehavior.

**Fix** (estimated 80 lines):
- **File**: `src/runtime/config/config_validator.h`
- Add post-parse validation:

```cpp
struct ConfigValidator {
    static bool Validate(const RuntimeConfig& config, std::string& error_msg);

    /* Example checks: */
    /* - network.limits.max_connections in [1, 100000] */
    /* - script.entry_scripts_dir is not empty */
    /* - database.pool_size in [1, 256] */
    /* - render.* fields are positive */
};
```

- Call `ConfigValidator::Validate()` after each `glaze::read_json()` call.
- On validation failure, log the error and refuse to apply the config.

### 13.4: Physics Scene Path Hardcoded

**Current State**: `engine.cc:159` hardcodes `"scene.json"` as the physics scene path. No config override exists.

**Fix** (estimated 10 lines):
- Add `physics.scene_path` to `RuntimeConfig`.
- Read from config instead of hardcoding.

### 13.5: Minor/Cosmetic Items (Documented, No Immediate Fix)

These items are noted for future reference but do not warrant dedicated plans:

| Item | Reason Deferred |
|------|----------------|
| Global hooks fragile (InitScript/UpdateScript/DestroyScript) | Requires redesign of script lifecycle; addressed by P1-9 multi-VM |
| Log no Lua source location | `log_bind.cc` hardcodes `"[lua] {}"` — cosmetic, add when Lua debug info API is integrated |
| lua_gc passive (only queries, never triggers) | Lua auto-GC is generally sufficient; tune per workload rather than forcing |
| class.lua isinstanceof O(depth) | Affects only deep inheritance + high-frequency calls; optimize when profiling shows it's a bottleneck |
| ParseOperationName buffer fixed 32 bytes | Truncation only on malformed input > 32 chars; low risk |
| Timer hierarchy too heavy (3 types + 5 clock sources) | Server only needs ms-level unified API; simplify when timer system is refactored |
| Hardcoded FetchResult timeout (5ms) | Covered implicitly by P2-13 (condition_variable) |
| Physics Recover() poll-sleep health check (100ms × 50) | Low impact; startup-only path |

## Acceptance Criteria

1. `conn:Send()` return value is checked; failures are logged
2. Circular imports are detected and reported with clear error messages
3. Config validation rejects out-of-range values at startup
4. Physics scene path is configurable via JSON config
5. All existing tests pass

## Dependencies: None | Estimated Effort: ~160 lines total
