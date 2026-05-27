# Engine API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。`Init()` / `Start()` / `Run()` / `Tick()` / `Cleanup()` 必须在单个 EventLoop 线程上串行调用。`Shutdown()` 是唯一的线程安全方法——可从任意线程调用以请求优雅关闭。 |
| **线程安全** | 部分。`Shutdown()` 使用原子操作设置 `running_` 标志，线程安全。`running()` / `frame_count()` 读取原子变量，线程安全。其他所有方法必须在主线程调用，不可跨线程使用。 |
| **回调线程** | 主线程。EventLoop 驱动信号处理（`SIGINT`/`SIGTERM`）、帧定时器、以及所有通过 `RunInLoop` 排队的回调。 |

## Overview

The Engine is the central application orchestrator. It manages the event loop, frame timing, script VM lifecycle, and coordinates all subsystems (logging, timers, networking, physics, profiling).

## Module

C++ API via `Engine` singleton. Not directly exposed to Lua, but drives the Lua runtime via `InitScript()`, `UpdateScript()`, and `DestroyScript()`.

## Lifecycle

### Standalone Mode

```
engine.Init(runtime_cfg, entry_scripts_dir);
engine.Run();  // Init → Start → event loop → Cleanup (blocks until Shutdown)
```

### Library Mode (host-driven)

```
engine.Init(config, &my_loop);
while (running) {
    my_loop_dispatch_pending();  // host drives IO
    engine.Tick();               // per-frame engine work
}
engine.Cleanup();
```

## Engine Methods (C++)

### `Init(runtime_cfg, entry_scripts_dir [, external_loop])`

Initializes the engine with configuration. In library mode, pass the host's EventLoop.

| Parameter | Type | Description |
|-----------|------|-------------|
| `runtime_cfg` | `RuntimeConfig` | Runtime configuration |
| `entry_scripts_dir` | `string` | Role-specific scripts directory |
| `external_loop` | `EventLoop*` | External event loop for library mode (optional) |

### `Start()`

Arms the frame timer and signal watchers on the engine's own loop. Standalone mode only.

### `Run()`

Convenience: `Start()` + enter event loop + `Cleanup()`. Blocks until `Shutdown()` is called.

### `Tick()`

Processes one frame: timer update + Lua update. Enforces frame rate limit — if called faster than `target_fps`, the call is a no-op.

- In standalone mode: called by the frame timer
- In library mode: the host calls this at its own cadence

### `Shutdown()`

Requests graceful shutdown. Thread-safe — can be called from any thread.

### `Cleanup()`

Releases all resources (Lua, timers, net bindings, timer manager). In standalone mode called automatically after the event loop exits. In library mode the host must call this before destroying the engine.

### Accessors

| Method | Returns | Description |
|--------|---------|-------------|
| `running()` | `bool` | Whether the engine is running |
| `frame_count()` | `uint64_t` | Total frames processed |
| `GetScriptVM()` | `ScriptVM&` | The main script VM |
| `GetEventLoop()` | `EventLoop*` | Active event loop |

## Frame Processing

Each frame:
1. Timer management (high-resolution timer wheel update)
2. Lua script update (`UpdateScript()` — calls a global `update()` function if defined)
3. Frame rate enforcement (frame interval from config)

## Shutdown Sequence

1. `Shutdown()` sets `running_ = false`
2. Event loop exits (standalone) or host stops calling `Tick()` (library)
3. `Cleanup()`:
   - Signal watchers destroyed
   - Frame timer cancelled
   - `DestroyScript()` called on ScriptVM
   - `ShutdownNetBindings()` — stops all TCP/UDP/KCP servers, releases HTTP callbacks
   - `TimerManager` shutdown
   - ScriptVM destroyed

## Signal Handling (Standalone Mode)

- `SIGINT` (Ctrl+C) → calls `Shutdown()`
- `SIGTERM` (Unix only) → calls `Shutdown()`
