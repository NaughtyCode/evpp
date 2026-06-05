# Engine API

Last synced: 2026-06-05.

`Engine` is the central runtime orchestrator. It owns the event loop, timer
manager, main script VM, hot-reload support, monitoring server, and subsystem
shutdown order.

## Threading Model

| Property | Value |
|----------|-------|
| Main thread | The EventLoop thread that initializes and drives the engine. |
| Thread-safe method | `Shutdown()` can be requested from any thread. |
| Main-thread methods | `Init()`, `Start()`, `Run()`, `Tick()`, `Cleanup()`, and most accessors must be used from the engine owner thread. |
| Lua callbacks | Lua lifecycle and binding callbacks execute on the main script VM owner thread unless a subsystem explicitly queues work back there. |

## Module

C++ API via `Engine::Instance()`. There is no `engine` Lua global. The engine
drives Lua by owning a `MainThreadScriptVM` and calling
`InitScript()`, `UpdateScript()`, and `DestroyScript()`.

## Lifecycle

### Standalone Mode

```cpp
engine.Init(runtime_cfg, entry_scripts_dir);
engine.Run();  // Start + event loop + Cleanup
```

### Library Mode

```cpp
engine.Init(runtime_cfg, entry_scripts_dir, external_loop);
while (running) {
    drive_external_loop_once();
    engine.Tick();
}
engine.Cleanup();
```

## Methods

### `Init(runtime_cfg, entry_scripts_dir [, external_loop])`

Initializes the engine.

| Parameter | Type | Description |
|-----------|------|-------------|
| `runtime_cfg` | `RuntimeConfig` | Runtime configuration. |
| `entry_scripts_dir` | `std::string` | Role-specific script directory, such as `resources/script/server`. |
| `external_loop` | `evpp::EventLoop*` | Optional host-owned event loop for library mode. |

During script setup, `Init()` creates `MainThreadScriptVM`, configures import
paths and script roots, calls
`MainThreadScriptVM::ExportRuntimeBindings(timer_mgr)`, loads entry scripts,
and calls `InitScript()`.

### `Start()`

Arms the frame timer and signal watchers on the engine-owned loop. Standalone
mode only. Does not enter the event loop.

### `Run()`

Calls `Start()`, enters the event loop, and calls `Cleanup()` after shutdown.
Standalone mode only.

### `Tick()`

Processes one frame in library mode or from the standalone frame timer.

Frame work:

1. Timer manager update.
2. Config callback flush on the main Lua VM.
3. Lua `UpdateScript()` call.
4. Deferred RPC callback drain.
5. Frame-rate enforcement using runtime frame config.

### `Shutdown()`

Requests graceful shutdown. This is the method intended for cross-thread
shutdown requests.

### `Cleanup()`

Releases subsystems in the required order. The main script VM remains alive
while binding shutdown code releases registry references and callback state.

Current cleanup order:

1. Physics shutdown.
2. Database shutdown.
3. Connection drain phase.
4. `MainThreadScriptVM::ShutdownNetworkBindings()`.
5. `MainThreadScriptVM::ShutdownTimerBindings()`.
6. `DestroyScript()`.
7. `MainThreadScriptVM::ShutdownProfilerBindings()`.
8. Lua memory report and script VM destruction.
9. Entity/timer manager shutdown.
10. Final runtime resources and logs.

### `ApplyConfigChanges()`

Applies pending config changes on the main thread.

### Accessors

| Method | Returns | Description |
|--------|---------|-------------|
| `running()` | `bool` | Whether the engine is running. |
| `initialized()` | `bool` | Whether initialization completed. |
| `cleanup_phase()` | `CleanupPhase` | Current cleanup phase. |
| `frame_count()` | `uint64_t` | Total frames processed. |
| `GetScriptVM()` | `ScriptVM&` | Main script VM returned as the base type. |
| `GetTimerManager()` | `TimerManager&` | Engine timer manager. |
| `GetEventLoop()` | `evpp::EventLoop*` | Active event loop. |
| `SetPhysicsResultHandler(handler)` | none | Registers a per-frame physics result consumer. |

## Cleanup Phases

```cpp
enum class CleanupPhase {
    NotStarted,
    PhysicsShutdown,
    DatabaseShutdown,
    NetworkShutdown,
    TimerShutdown,
    ScriptDestroyed,
    FinalLogs,
    Complete
};
```

The phase enum is used for diagnostics and timeout reporting during cleanup.

## Signal Handling

Standalone mode installs signal watchers on the engine loop:

- `SIGINT` requests `Shutdown()`.
- `SIGTERM` requests `Shutdown()` on Unix-like platforms.
- `SIGHUP` is used for config reload on Unix-like platforms when enabled.
