# ScriptVM API

Last synced: 2026-06-05.

`ScriptVM` is the RAII wrapper around one Lua `lua_State`. It owns the Lua
state, import system, callback storage, script roots, and custom pointer store.
`MainThreadScriptVM` derives from `ScriptVM` and is the only VM type that
receives the full main runtime binding set.

## Threading Model

| Property | Value |
|----------|-------|
| Owner thread | The C++ thread that constructs the VM. |
| Lua state thread safety | Not thread-safe. A `lua_State` must be accessed only from its owner thread unless a subsystem explicitly marshals work back to that thread. |
| `ScriptVM::IsOwnerThread()` | Returns whether the current C++ thread owns the VM. |
| `ScriptVM::IsMainThreadVM()` | Returns `false` for plain `ScriptVM`. |
| `MainThreadScriptVM::IsMainThreadVM()` | Returns `true`. |

All direct Lua C API use through `GetState()` must follow the owner-thread
rule. The main runtime binding export and shutdown helpers enforce this rule.

## VM Types

### `ScriptVM`

Base VM used for generic Lua execution and purpose-specific VM contexts. It
does not imply that the full engine Lua API is present.

### `MainThreadScriptVM`

Main engine script VM. It centralizes the runtime binding export and shutdown
logic that belongs only to the main thread:

```cpp
void ExportRuntimeBindings(TimerManager& timer_mgr);
void ShutdownNetworkBindings();
void ShutdownTimerBindings();
void ShutdownProfilerBindings();
```

`ExportRuntimeBindings()` currently exports logging, timer, network, entity,
MessagePack, JSON, space, AOI, RPC, auth, config, profiler, import, and
optional memory/database modules. See `docs/spec/lua-runtime-api.md` for the
Lua-facing API list.

The `profiler` Lua module is exported only through `MainThreadScriptVM` and
each `profiler.*` API checks that it is called on the VM owner thread.

## Construction

```cpp
ScriptVM vm(LuaSandboxLevel::Full);
MainThreadScriptVM main_vm(LuaSandboxLevel::Full);
```

Both VM classes are move-only and non-copyable.

## Script Lifecycle

The C++ methods below call global Lua functions with matching names. Missing
Lua functions are silently ignored.

| C++ method | Lua global called | When used |
|------------|-------------------|-----------|
| `InitScript()` | `InitScript()` | Once after scripts and C APIs are loaded. |
| `UpdateScript()` | `UpdateScript()` | Once per frame. |
| `DestroyScript()` | `DestroyScript()` | During engine cleanup. |

Lua entry scripts should define these names:

```lua
function InitScript()
    -- Called once after runtime bindings and scripts are loaded.
end

function UpdateScript()
    -- Called once per engine frame.
end

function DestroyScript()
    -- Called during shutdown.
end
```

Shared runtime modules under `resources/script/runtime` should not define these
global lifecycle hooks. They belong to the role entry script.

## Script Execution

### `DoString(script [, chunk_name [, error_out [, result_out]]])`

Executes a Lua source string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `script` | `std::string_view` | Lua source code. |
| `chunk_name` | `std::string_view` | Name used in Lua error messages. Defaults to `"string"`. |
| `error_out` | `std::string*` | Optional destination for error text. |
| `result_out` | `std::string*` | Optional destination for a string result from the stack top. |

Returns `true` on success.

### `DoFile(filename [, error_out])`

Executes one Lua file and returns `true` on success.

### `DoDirectory(dir_path)`

Executes all `.lua` files found directly in a directory. The scan is
non-recursive and returns the number of failed files.

## C Function And Module Registration

| C++ method | Description |
|------------|-------------|
| `RegisterFunction(name, func)` | Registers one Lua C function as a global. |
| `RegisterFunctions(functions)` | Registers every function in a `luaL_Reg` array as a global. |
| `RegisterModule(name, functions)` | Registers a `luaL_Reg` array as a global module table. |
| `RegisterModuleOpen(name, openf, make_global)` | Registers a module with a `luaL_requiref` style open function and stores it in `package.preload`. |
| `RegisterCallback(name, callback)` | Registers a stored `std::function<int(lua_State*)>` as a global. |

The callback registered by `RegisterCallback()` follows the Lua C calling
convention: it receives `lua_State*`, pushes return values, and returns the
number of values pushed.

## Import And Script Roots

| C++ method | Description |
|------------|-------------|
| `GetImporter()` | Returns the per-VM `ScriptImporter`. |
| `SetImportPath(scripts_dir)` | Sets the importer search path. |
| `SetScriptRoot(root_dir)` | Replaces script roots with one root. |
| `SetScriptRoots(root_dirs)` | Replaces script roots with multiple roots. |
| `GetScriptRoots()` | Returns configured script roots. |
| `GetDefaultModuleNameForFile(filename)` | Derives a default module name for one file. |
| `BuildDefaultModuleNameForFile(filename)` | Static default module-name derivation. |
| `BuildModuleNameForFile(filename, roots, separator)` | Static module-name derivation with explicit roots and separator. |

File-backed script loads derive default module names from the script path
relative to one configured root. Path separators are converted to underscores.
For example, `<root>/runtime/net/init.lua` becomes `runtime_net_init`.

## Custom Pointer Store

The custom pointer store is a per-VM void-pointer array backed by Lua global
state. It is useful for associating C++ objects with a Lua state without adding
more registry keys.

| C++ method | Description |
|------------|-------------|
| `ReserveCustomPtrSlots(n)` | Pre-allocates pointer slots. |
| `SetCustomPtr(index, ptr)` | Stores a pointer at a 1-based index. |
| `GetCustomPtr(index)` | Returns the pointer at a 1-based index or `nullptr`. |
| `GetCustomPtrAs<T>(index)` | Typed wrapper around `GetCustomPtr`. |
| `PushCustomPtr(ptr)` | Appends a pointer and returns its new 1-based index. |
| `SetNullCustomPtr(index)` | Sets an existing slot to `nullptr`. |
| `ClearCustomPtrs()` | Clears all stored pointers. |
| `CustomPtrCount()` | Returns the number of stored slots. |
| `CustomPtrCapacity()` | Returns current store capacity. |
| `HasCustomPtr(index)` | Checks whether a slot exists and is non-null. |
| `FindCustomPtr(ptr)` | Returns the 1-based index for a pointer, or `-1`. |
| `ContainsCustomPtr(ptr)` | Checks whether a pointer is stored. |
| `CopyCustomPtrsTo(dst, max)` | Copies up to `max` pointers out. |
| `CopyCustomPtrsFrom(src, count)` | Replaces the store from an external array. |

## Utilities

| C++ method | Description |
|------------|-------------|
| `GetState()` | Returns the raw `lua_State*`. |
| `SetGlobal<T>(name, value)` | Sets a global scalar value. Supported specializations include int, double, string, string view, C string, and bool. |
| `ToString([index])` | Converts a stack value to string and pops the stack top. |
| `LuaVersion()` | Returns the Lua version string. |

## Engine Integration

The engine owns a `std::unique_ptr<MainThreadScriptVM>`, but
`Engine::GetScriptVM()` returns it as a `ScriptVM&` for compatibility with code
that only needs the base API.

During initialization:

1. `Engine::Init()` creates `MainThreadScriptVM`.
2. It configures import path and script roots.
3. It calls `MainThreadScriptVM::ExportRuntimeBindings(timer_mgr)`.
4. It loads entry scripts.
5. It calls `InitScript()`.

During each frame, `Engine::FrameLoop()` flushes config callbacks, calls
`UpdateScript()`, then drains deferred RPC callbacks.

During cleanup, the main VM is kept alive while network, timer, Lua lifecycle,
and profiler shutdown helpers release registry references and callback state.
