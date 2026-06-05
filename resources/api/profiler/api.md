# Profiler API

Last synced: 2026-06-05.

The profiler has two control surfaces:

- C++: `ProfilerManager` and `ENGINE_PROFILE_*` instrumentation macros.
- Lua: global `profiler` table exported only to `MainThreadScriptVM`.

Trace events may be emitted from instrumented C++ code paths, but profiler
session control and all Lua `profiler.*` calls are expected to run from the
main script VM owner thread.

## Lua Export Scope

`profiler` is registered by:

```cpp
bool ExportProfiler(MainThreadScriptVM& vm);
```

`MainThreadScriptVM::ExportRuntimeBindings(TimerManager&)` is the normal export
path. The module is not exported to plain `ScriptVM`, physics VMs, database
thread VMs, or space-local VMs by default.

Every Lua API checks at call time that:

- the registry state still belongs to the main-thread Lua VM;
- the module is not shutting down;
- the current C++ thread is the VM owner thread.

Violations raise Lua errors such as `profiler: API must be called from main
thread`.

## Lua Runtime Switches

```lua
profiler.set_runtime_enabled(true)
local enabled = profiler.is_runtime_enabled()

local mask, names = profiler.set_enabled_groups({ "physics", "script" })
profiler.enable_groups("network,rpc")
profiler.disable_groups(profiler.GROUP_DATABASE)
profiler.set_group_enabled("frame", true)
```

| API | Returns | Description |
|-----|---------|-------------|
| `profiler.set_runtime_enabled(enabled)` | none | Enables or disables event emission without rebuilding. |
| `profiler.is_runtime_enabled()` | boolean | Reads the runtime master switch. |
| `profiler.enabled_groups()` | mask, names | Reads the current event-group mask and formatted group names. |
| `profiler.set_enabled_groups(groups)` | mask, names | Replaces the current event-group mask. |
| `profiler.enable_groups(groups)` | mask, names | Adds groups to the enabled mask. |
| `profiler.disable_groups(groups)` | mask, names | Removes groups from the enabled mask. |
| `profiler.set_group_enabled(group, enabled)` | mask, names | Enables or disables one group. |
| `profiler.is_group_enabled(group)` | boolean | Checks whether one group is enabled and the runtime master switch allows it. |

`groups` accepts:

- a non-negative integer bit mask;
- a string separated by `,`, `;`, or `|`;
- a flat Lua table containing group strings and/or integer masks.

Nested group tables and unsupported mask bits raise Lua errors. Empty strings,
`none`, `off`, and `disabled` resolve to `GROUP_NONE`.

## Lua Group Helpers

| API | Returns | Description |
|-----|---------|-------------|
| `profiler.parse_groups(groups)` | mask, names | Parses a group spec without changing profiler state. |
| `profiler.format_groups(groups)` | names | Formats a group spec as a canonical group-name string. |
| `profiler.group_from_name(name)` | mask, canonical_name | Resolves a group name. |
| `profiler.group_from_category(category)` | mask, canonical_name | Resolves a Perfetto category name. |
| `profiler.group_name(group)` | name | Returns the canonical name for one group. |
| `profiler.group_category(group)` | category | Returns the Perfetto category for one group. |
| `profiler.list_groups()` | table | Returns rows with `name`, `category`, `mask`, and `enabled`. |

Supported constants:

```lua
profiler.GROUP_NONE
profiler.GROUP_ALL
profiler.GROUP_ENGINE
profiler.GROUP_FRAME
profiler.GROUP_TIMER
profiler.GROUP_PHYSICS
profiler.GROUP_SCRIPT
profiler.GROUP_ENTITY
profiler.GROUP_SPACE
profiler.GROUP_AOI
profiler.GROUP_AUTH
profiler.GROUP_VM
profiler.GROUP_NETWORK
profiler.GROUP_RPC
profiler.GROUP_DATABASE
profiler.GROUP_MONITORING
profiler.GROUP_CONFIG
```

## Lua Session And Trace APIs

```lua
local ok = profiler.initialize({
    output_path = "trace.perfetto-trace",
    buffer_size_kb = 32768,
    duration_ms = 0,
    flush_interval_ms = 5000,
    write_into_file = false,
    runtime_enabled = true,
    enabled_event_groups = "frame,timer,script",
})

profiler.start_session()
profiler.stop_session()
local trace_bytes = profiler.read_trace()
local path = profiler.save_trace()
```

| API | Returns | Description |
|-----|---------|-------------|
| `profiler.initialize([config])` | boolean | Initializes the profiler manager. |
| `profiler.shutdown()` | none | Shuts down the profiler manager. |
| `profiler.start_session()` | boolean | Starts trace capture. |
| `profiler.stop_session()` | none | Stops trace capture. |
| `profiler.is_enabled()` | boolean | Returns profiler compile/runtime availability. |
| `profiler.is_initialized()` | boolean | Returns whether the manager is initialized. |
| `profiler.is_active()` | boolean | Returns whether a trace session is active. |
| `profiler.flush()` | none | Flushes pending trace data. |
| `profiler.read_trace()` | binary string | Reads cached trace data into Lua. |
| `profiler.save_trace()` | path or `nil` | Saves trace data to the configured/default path. |
| `profiler.save_trace_exact(path)` | boolean | Saves trace data to an exact path. |
| `profiler.last_saved_path()` | string | Returns the last successful save path. |
| `profiler.cached_trace_size()` | integer | Returns cached trace byte count. |
| `profiler.clear_cached_trace()` | none | Clears cached trace data. |
| `profiler.status()` | table | Returns a status snapshot. |

`profiler.initialize()` config fields:

| Field | Type | Description |
|-------|------|-------------|
| `output_path` | string | Trace output path. |
| `buffer_size_kb` | uint32 | Trace buffer size in KiB. |
| `duration_ms` | uint32 | Maximum session duration. `0` means unlimited. |
| `flush_interval_ms` | uint32 | Periodic flush interval. |
| `write_into_file` | boolean | Write directly to file instead of buffering. |
| `runtime_enabled` | boolean | Initial runtime master switch. |
| `enabled_event_groups` | groups | Initial enabled event groups. |

`profiler.status()` returns:

```lua
{
    enabled = true,
    initialized = true,
    active = false,
    runtime_enabled = true,
    enabled_event_groups = profiler.GROUP_ALL,
    enabled_event_group_names = "engine,frame,...",
    cached_trace_size = 0,
    last_saved_path = "",
}
```

## C++ ProfilerManager

```cpp
auto& profiler = ProfilerManager::Get();

ProfilerConfig cfg;
profiler.Initialize(cfg);
profiler.StartSession();
profiler.StopSession();
profiler.Flush();
auto bytes = profiler.ReadTrace();
auto path = profiler.SaveTrace();
profiler.Shutdown();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `Initialize(cfg)` | `bool` | Sets up Perfetto tracing with `ProfilerConfig`. |
| `Shutdown()` | none | Releases profiler resources. |
| `StartSession()` | `bool` | Begins recording. |
| `StopSession()` | none | Ends recording. |
| `Flush()` | none | Flushes trace data. |
| `ReadTrace()` | `std::vector<char>` | Reads cached trace bytes. |
| `SaveTrace()` | `std::string` | Saves to configured/default path. |
| `SaveTraceExact(path)` | `bool` | Saves to an exact path. |
| `LastSavedPath()` | `std::string` | Returns last save path. |
| `CachedTraceSize()` | `size_t` | Returns cached trace byte count. |
| `ClearCachedTrace()` | none | Clears cached trace data. |
| `IsInitialized()` | `bool` | Returns initialization state. |
| `IsActive()` | `bool` | Returns session state. |
| `IsEnabled()` | `bool` | Returns compile/runtime availability. |
| `SetRuntimeEnabled(bool)` | none | Sets the runtime master switch. |
| `IsRuntimeEnabled()` | `bool` | Reads the runtime master switch. |
| `SetEnabledEventGroups(mask)` | none | Replaces enabled event groups. |
| `EnableEventGroups(mask)` | none | Enables groups. |
| `DisableEventGroups(mask)` | none | Disables groups. |
| `EnabledEventGroups()` | mask | Reads enabled groups. |

## ProfilerConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `output_path` | `std::string` | `"trace.perfetto-trace"` | Trace output file path. |
| `buffer_size_kb` | `uint32_t` | `32768` | Trace buffer size in KiB. |
| `duration_ms` | `uint32_t` | `0` | Maximum session duration. |
| `flush_interval_ms` | `uint32_t` | `5000` | Periodic flush interval. |
| `write_into_file` | `bool` | `false` | Direct file writing mode. |
| `runtime_enabled` | `bool` | build/runtime default | Initial runtime master switch. |
| `enabled_event_groups` | mask | all supported groups | Initial event-group mask. |

## Event Groups And Categories

| Group | Perfetto category | Typical scope |
|-------|-------------------|---------------|
| `engine` | `engine` | Engine lifecycle and broad runtime scopes. |
| `frame` | `engine` | Frame loop processing. |
| `timer` | `engine.timer` | Timer scheduling and callbacks. |
| `physics` | `engine.physics` | Physics simulation. |
| `script` | `engine.script` | Lua execution and binding operations. |
| `entity` | `engine.entity` | Entity lifecycle and components. |
| `space` | `engine.space` | Space lifecycle and routing. |
| `aoi` | `engine.aoi` | Area-of-interest operations. |
| `auth` | `engine.auth` | Auth and sessions. |
| `vm` | `engine.vm` | Lua VM lifecycle. |
| `network` | `engine.net` | Network I/O. |
| `rpc` | `engine.rpc` | RPC dispatch. |
| `database` | `engine.db` | Database operations. |
| `monitoring` | `engine.monitoring` | Monitoring/admin operations. |
| `config` | `engine.config` | Config loading and reload. |

## C++ Instrumentation

Use helpers from `runtime/profiler/profiler_events.h` where possible. They
route events through the runtime switch and event-group mask before touching
Perfetto.

Common patterns:

```cpp
ENGINE_PROFILE_SCOPE("engine.script", "LuaCall");
ENGINE_PROFILE_FRAME();
ENGINE_PROFILE_SCRIPT_EXPORT("profiler");
```

## Notes

- When profiler support is compiled out, Lua `profiler.is_enabled()` reports
  false and session operations become no-op/failure according to
  `ProfilerManager`.
- Trace files use Perfetto's binary format and can be opened in
  https://ui.perfetto.dev.
- The Lua binding state is removed by
  `MainThreadScriptVM::ShutdownProfilerBindings()` before the VM is destroyed.
