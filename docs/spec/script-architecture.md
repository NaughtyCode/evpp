# Script Architecture Specification

## Directory Layout

### Scripts

```
resources/script/
    runtime/          Shared Lua modules (loaded by both client and server)
        README.md
        init.lua      Runtime entry point
        *.lua         Utility modules, base classes, shared game logic
    client/           Client entry scripts (loaded by src/client)
        README.md
        init.lua      Client entry point
        *.lua         Client-only modules
    server/           Server entry scripts (loaded by src/server)
        README.md
        init.lua      Server entry point
        *.lua         Server-only modules
    tests/            Test scripts (not auto-loaded)
```

### Config

```
resources/config/
    runtime/
        runtime.json     System-level config: log, frame, runtime scripts_dir
    client/
        client.json      Client config: entry scripts_dir
    server/
        server.json      Server config: entry scripts_dir, HTTP, MessagePack
```

## Loading Mechanism

### Server (`src/server`)

1. `server/main.cc` calls `ConfigManager::Instance().Load("resources/config")`, which
   loads `runtime/runtime.json` (required) and `server/server.json` (optional).
2. `Engine::Init(runtime_cfg, server_cfg.scripts_dir)` is called:
   - `runtime_cfg.scripts_dir` = `resources/script/runtime`
   - `server_cfg.scripts_dir` = `resources/script/server`
3. `Engine::Init()` calls `script_vm_->DoDirectory("resources/script/server")`, which
   loads all `.lua` files in that directory (non-recursive).
4. The entry script (`init.lua`) executes `import("runtime.init")` as its first action,
   loading the runtime module entry point from `resources/script/runtime/init.lua`.
5. The entry script then defines `InitScript()`, `UpdateScript()`, `DestroyScript()`.
6. `Engine::Init()` calls `script_vm_->InitScript()` to fire the Lua `InitScript()` hook.

### Client (`src/client`)

1. `game_client_init()` uses default `RuntimeConfig` and `ClientConfig`, whose
   defaults are:
   - `runtime_cfg.scripts_dir` = `resources/script/runtime`
   - `client_cfg.scripts_dir` = `resources/script/client`
2. If a `config_dir` is passed, config is loaded from disk via `ConfigManager::Load()`
   (overriding the defaults).
3. `Engine::Init(runtime_cfg, client_cfg.scripts_dir, loop)` is called.
4. `Engine::Init()` calls `script_vm_->DoDirectory("resources/script/client")`.
5. The entry script (`init.lua`) executes `import("runtime.init")` as its first action.
6. Remaining flow is identical to the server.

### Import Path Resolution

The import system (`ScriptImporter`) derives its search path from the runtime scripts
directory. If `runtime_cfg.scripts_dir` is `resources/script/runtime`, the parent
directory `resources/script/` is used as the import search root.

This ensures `import("runtime.init")` resolves to `resources/script/runtime/init.lua`,
and `import("runtime.helpers")` resolves to `resources/script/runtime/helpers.lua`,
regardless of whether the engine is running in client or server mode.

## Lua API References

- [Lua Runtime API](lua-runtime-api.md)
- [Lua JSON API](lua-json-api.md)
- [DB BSON Table Codec API](../../resources/api/db_bson/api.md)

## Engine::Init() Signature

```cpp
void Init(const RuntimeConfig& runtime_cfg,
          const std::string& entry_scripts_dir,
          evpp::EventLoop* external_loop = nullptr);
```

| Parameter | Source (server) | Source (client) |
|-----------|-----------------|-----------------|
| `runtime_cfg` | `runtime/runtime.json` | Default or `runtime/runtime.json` |
| `entry_scripts_dir` | `server_cfg.scripts_dir` | `client_cfg.scripts_dir` |
| `external_loop` | `nullptr` (owned loop) | Host-provided EventLoop |

## Config Structs (C++)

```cpp
struct RuntimeConfig {
    std::string resource_dir = "resources";
    LogConfig log;
    FrameConfig frame;
    HotReloadConfig hot_reload;  // default startup_delay_ms = 60000
    std::string scripts_dir = "resources/script/runtime";
};

struct ClientConfig {
    std::string scripts_dir = "resources/script/client";
};

struct ServerConfig {
    HttpConfig http;
    MsgpackConfig msgpack;
    std::string scripts_dir = "resources/script/server";
};
```

## Lifecycle Hooks

The engine calls three global Lua functions. Only the role entry script defines them:

| Function | When called | Purpose |
|----------|-------------|---------|
| `InitScript()` | Once, after all scripts loaded | One-time setup (timers, listeners, config) |
| `UpdateScript()` | Every frame | Per-frame logic |
| `DestroyScript()` | Once, on shutdown | Cleanup (cancel timers, close handles) |

If a function is not defined, the engine silently skips it (no-op).

## Runtime Module Conventions

Modules in `resources/script/runtime/` are shared library code:

- `init.lua` is the runtime entry point, loaded via `import("runtime.init")`.
- Each `.lua` file is a self-contained module. Return a table to expose a public API.
- Do NOT define `InitScript()` / `UpdateScript()` / `DestroyScript()` in runtime
  modules — those belong exclusively to the role entry scripts.
- Use `import("runtime.other_module")` to depend on another runtime module.

## CLI Override (Server Only)

The server supports overriding config values via command line:

```
./main --log_dir=custom/logs --scripts_dir=resources/script/custom
```

- `--log_dir=` overrides `RuntimeConfig::log.dir`.
- `--scripts_dir=` overrides `ServerConfig::scripts_dir` (the entry scripts directory).

The runtime `scripts_dir` (used for import path derivation) is not overridden by CLI.

## Design Rationale

- **Separation of concerns**: Runtime code is shared; client and server each have
  their own entry points, configs, and role-specific modules.
- **Config-per-role**: Each target (runtime/client/server) has its own config file
  in a dedicated subdirectory. Client and server configs are optional at load time.
- **Explicit loading**: The entry script explicitly loads the runtime module via
  `import("runtime.init")`, making the dependency order visible in code.
- **Non-recursive DoDirectory**: Only the role directory is scanned for `.lua` files.
  Runtime modules are loaded through the import system, giving them proper module
  caching and namespace isolation.
- **Derived import path**: The import search root is derived from `runtime_cfg.scripts_dir`
  (parent directory), so `import("runtime.*")` always resolves correctly regardless
  of configuration.
- **Delayed hot-reload watcher**: Script hot-reload starts in idle state during
  `Engine::Init()`. After runtime startup succeeds, `hot_reload.startup_delay_ms`
  controls when `FileWatcher` starts scanning for `.lua` changes.
