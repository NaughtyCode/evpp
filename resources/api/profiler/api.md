# Profiler System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 任意线程。Perfetto tracing SDK 内置线程安全保证——trace event 记录（`TRACE_EVENT_BEGIN`/`END` 等）可在任意线程上调用，Perfetto 内部处理线程局部缓冲区。`ProfilerManager` 的控制方法（`Initialize` / `StartSession` / `StopSession` / `SaveTrace`）应在单个控制线程上调用。 |
| **线程安全** | 是。Perfetto SDK 使用线程局部存储和 lock-free 环形缓冲区，每个线程向自己的缓冲区写入 trace event。`ProfilerManager` 本身无内部锁——控制方法假定单线程调用。 |
| **回调线程** | 无回调。所有函数均为同步调用。Flush 将各线程的缓冲区数据提交到中央 trace 缓冲区。 |

## Overview

The Profiler system provides built-in performance tracing using Perfetto. It instruments the engine's frame loop, subsystem initialization, and key operations to produce traces viewable in Perfetto's trace viewer.

## Module

C++ API via `ProfilerManager` singleton. Not directly exposed to Lua. Traces are written to files for offline analysis.

## ProfilerManager (C++)

### Lifecycle

```
ProfilerManager::Get().Initialize(cfg);   // Set up tracing
ProfilerManager::Get().StartSession();    // Begin recording
// ... application runs ...
ProfilerManager::Get().StopSession();     // End recording
ProfilerManager::Get().SaveTrace();       // Write trace to file
ProfilerManager::Get().Shutdown();        // Clean up
```

### Methods

#### `Initialize(cfg)`

Initializes the profiler with configuration.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cfg` | `ProfilerConfig` | Profiler configuration |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `bool` | `true` on success |

#### `StartSession()`

Starts a tracing session. Trace events are recorded until `StopSession()` is called.

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `bool` | `true` on success |

#### `StopSession()`

Stops the active tracing session.

#### `Flush()`

Flushes buffered trace data to the output.

#### `ReadTrace()`

Reads the recorded trace data into memory.

| Returns | Type | Description |
|---------|------|-------------|
| `data` | `vector<char>` | Raw trace binary data |

#### `SaveTrace()`

Saves the trace to the configured output path.

#### `SaveTraceExact(path)`

Saves the trace to a specific file path.

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `string` | Output file path |

#### Status

| Method | Returns | Description |
|--------|---------|-------------|
| `IsActive()` | `bool` | Whether a session is currently recording |
| `IsEnabled()` | `bool` | Global profiler compilation flag (static) |

## ProfilerConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `output_path` | `string` | `"trace.perfetto-trace"` | Trace output file path |
| `buffer_size_kb` | `uint32_t` | `32768` | Trace buffer size in KB |
| `duration_ms` | `uint32_t` | `0` | Maximum session duration (0 = unlimited) |
| `flush_interval_ms` | `uint32_t` | `5000` | Periodic flush interval |
| `write_into_file` | `bool` | `false` | Write directly to file instead of buffering |

## Instrumentation Categories

The engine instruments the following subsystems:

| Category | Description |
|----------|-------------|
| `script_export` | API export timing (log, timer, net, msgpack, mongo, db_service) |
| `frame` | Per-frame processing |
| `physics` | Physics tick timing |
| `network` | Network I/O operations |

## Profile Event Macros (C++)

```
ENGINE_PROFILE_SCOPED(name)      — Scoped event for the current function/block
ENGINE_PROFILE_SCRIPT_EXPORT(n)  — Script export timing
ENGINE_PROFILE_FRAME()           — Frame processing
ENGINE_PROFILE_PHYSICS_TICK()    — Physics tick
```

## Notes

- Profiling is conditionally compiled based on build configuration
- Traces use the Perfetto binary format and can be viewed at https://ui.perfetto.dev
- Buffer size and flush interval should be tuned based on expected trace duration
