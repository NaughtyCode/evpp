# Profiler Lua Binding Main-Thread Report

Date: 2026-06-05

## 1. Goal

Add a `profiler` Lua module that exposes the profiler runtime control API to
Lua while enforcing two constraints:

- The module is exported only to the main-thread `ScriptVM`.
- Every exported Lua API checks at call time that it is running on the VM owner
  thread.

This complements the profiler runtime switch work in
`perfetto-runtime-switch-analysis-2026-06-05.md`.

## 2. Binding Entry Point

New files:

- `src/runtime/script/profiler_bind.h`
- `src/runtime/script/profiler_bind.cc`

The public entry points are:

```cpp
bool ExportProfiler(MainThreadScriptVM& vm);
void ShutdownProfilerBindings(MainThreadScriptVM& vm);
```

`MainThreadScriptVM` is the only VM type that can receive this module. Its
runtime binding export owns the main-thread Lua surface and exports the module:

```cpp
void MainThreadScriptVM::ExportRuntimeBindings(TimerManager& timer_mgr) {
    ...
    script::ExportProfiler(*this);
    ...
}
```

Database, physics, validation, and other dedicated VMs do not call
`MainThreadScriptVM::ExportRuntimeBindings()`, so they do not receive the
`profiler` module by default. The C++ type signature also prevents exporting
the profiler module to a plain `ScriptVM`.

## 3. Main-Thread Enforcement

`ScriptVM` records the thread that constructs the VM:

```cpp
std::thread::id owner_thread_id_{};
```

It exposes read-only checks. `ScriptVM::IsMainThreadVM()` returns `false`;
`MainThreadScriptVM` overrides it to return `true`.

```cpp
bool IsOwnerThread() const noexcept;
virtual bool IsMainThreadVM() const noexcept;
```

The main-thread VM also enforces owner-thread calls at the centralized runtime
binding entry points:

```cpp
void MainThreadScriptVM::ExportRuntimeBindings(TimerManager& timer_mgr);
void MainThreadScriptVM::ShutdownNetworkBindings();
void MainThreadScriptVM::ShutdownTimerBindings();
void MainThreadScriptVM::ShutdownProfilerBindings();
```

These functions throw if called from a non-owner thread before touching the Lua
state.

The Lua binding stores a small per-VM state in the Lua registry:

```cpp
struct ProfilerBindState {
    lua_State* main_state;
    std::thread::id owner_thread_id;
    bool shutting_down;
};
```

Every Lua C function starts with `CheckMainThread(L)`. This reads the registry
state, rejects missing/shutting-down bindings, and compares the current thread
with the owner thread captured at export time.

Because the state is stored in the Lua registry, Lua coroutines in the same VM
share the same binding state and are allowed when executed on the owner thread.

## 4. Lua API Surface

The module is registered as global table `profiler`.

Runtime switches:

- `profiler.set_runtime_enabled(enabled)`
- `profiler.is_runtime_enabled()`
- `profiler.enabled_groups() -> mask, names`
- `profiler.set_enabled_groups(mask_or_names_or_table) -> mask, names`
- `profiler.enable_groups(mask_or_names_or_table) -> mask, names`
- `profiler.disable_groups(mask_or_names_or_table) -> mask, names`
- `profiler.set_group_enabled(group, enabled) -> mask, names`
- `profiler.is_group_enabled(group) -> bool`

Group helpers:

- `profiler.parse_groups(mask_or_names_or_table) -> mask, names`
- `profiler.format_groups(mask_or_names_or_table) -> names`
- `profiler.group_from_name(name) -> mask, canonical_name`
- `profiler.group_from_category(category) -> mask, canonical_name`
- `profiler.group_name(group) -> name`
- `profiler.group_category(group) -> category`
- `profiler.list_groups() -> table`

Group masks passed from Lua are validated strictly: unknown group names,
negative masks, masks with unsupported bits, and combined masks passed to
single-group APIs are rejected with Lua errors instead of being silently
clamped.

Profiler manager/session/trace operations:

- `profiler.initialize([config_table]) -> bool`
- `profiler.shutdown()`
- `profiler.start_session() -> bool`
- `profiler.stop_session()`
- `profiler.is_enabled() -> bool`
- `profiler.is_initialized() -> bool`
- `profiler.is_active() -> bool`
- `profiler.flush()`
- `profiler.read_trace() -> binary_string`
- `profiler.save_trace() -> path_or_nil`
- `profiler.save_trace_exact(path) -> bool`
- `profiler.last_saved_path() -> string`
- `profiler.cached_trace_size() -> integer`
- `profiler.clear_cached_trace()`
- `profiler.status() -> table`

Constants:

- `profiler.GROUP_NONE`
- `profiler.GROUP_ALL`
- `profiler.GROUP_ENGINE`
- `profiler.GROUP_FRAME`
- `profiler.GROUP_TIMER`
- `profiler.GROUP_PHYSICS`
- `profiler.GROUP_SCRIPT`
- `profiler.GROUP_ENTITY`
- `profiler.GROUP_SPACE`
- `profiler.GROUP_AOI`
- `profiler.GROUP_AUTH`
- `profiler.GROUP_VM`
- `profiler.GROUP_NETWORK`
- `profiler.GROUP_RPC`
- `profiler.GROUP_DATABASE`
- `profiler.GROUP_MONITORING`
- `profiler.GROUP_CONFIG`

## 5. Lua Usage Examples

Enable only physics and script probes:

```lua
profiler.set_enabled_groups({ "physics", "script" })
```

Disable all profiler event emission without rebuilding:

```lua
profiler.set_runtime_enabled(false)
```

Disable one group temporarily:

```lua
profiler.set_group_enabled("physics", false)
```

Inspect current state:

```lua
local status = profiler.status()
print(status.runtime_enabled, status.enabled_event_group_names)
```

## 6. Lifecycle

`Engine::Cleanup()` now calls:

```cpp
script_vm_->ShutdownProfilerBindings();
```

This removes the registry state and global `profiler` table before the VM is
closed. Unit-test fixtures also call `ShutdownProfilerBindings()` to avoid
state leakage across tests.

## 7. Tests

New test target:

- `test_profiler_bind`

Covered scenarios:

- `MainThreadScriptVM::ExportRuntimeBindings()` exposes `profiler` on the main VM.
- Lua can update runtime enabled state and group masks.
- Invalid Lua runtime/group/config arguments are rejected with Lua errors.
- Self-referential group tables are rejected instead of recursing indefinitely.
- Calling `profiler` APIs from a non-owner thread raises a Lua error.
- Runtime binding export from a non-owner thread is rejected before Lua state mutation.

Verification run:

```powershell
cmake --build artifacts/build-profiler-switch --target test_profiler_bind test_profiler test_script_bind --config Debug -- /m /nodeReuse:false
artifacts/bin/Debug/test_profiler_bind.exe
artifacts/bin/Debug/test_profiler.exe
artifacts/bin/Debug/test_script_bind.exe

cmake --build artifacts/build-profiler-switch-off --target test_profiler_bind test_profiler test_script_bind --config Debug -- /m /nodeReuse:false
artifacts/bin/Debug/test_profiler_bind.exe
artifacts/bin/Debug/test_profiler.exe
artifacts/bin/Debug/test_script_bind.exe

git diff --check
```

Results:

- Profiler ON `test_profiler_bind`: all tests passed, 14 assertions in 5 cases.
- Profiler ON `test_script_bind`: all tests passed, 18 assertions in 8 cases.
- Profiler OFF `test_profiler_bind`: all tests passed, 14 assertions in 5 cases.
- Profiler OFF `test_profiler`: all tests passed, 20 assertions in 3 cases.
- Profiler OFF `test_script_bind`: all tests passed, 18 assertions in 8 cases.
- Profiler ON `test_profiler`: all tests passed, 33 assertions in 3 cases.
- Additional same-day review coverage passed on the profiler-ON build:
  `test_hotreload` (179 assertions in 50 cases), `test_vm_coverage`
  (151 assertions in 48 cases), `test_space` (96 assertions in 43 cases),
  and `test_game_config` (278 assertions in 45 cases).
- `git diff --check`: no whitespace errors; only CRLF conversion warnings.

MSVC emitted the existing `LNK4098` default-library warning during test links.
No profiler binding failures were observed.
