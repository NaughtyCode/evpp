# ScriptVM API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。lua_State 非线程安全——所有 ScriptVM 方法必须在拥有该 VM 的线程上调用，且不能与回调并发调用。 |
| **线程安全** | 否。lua_State 本身不提供内部锁或线程安全保护。`CustomPtrStore` 也不提供同步机制。每个 VM 实例与单个线程绑定，不可跨线程共享。 |
| **回调线程** | 无回调。ScriptVM 自身不触发回调——它提供 C 函数注册机制，这些 C 函数在 Lua 调用它们时在调用者线程上执行。`InitScript()` / `UpdateScript()` / `DestroyScript()` 在调用者线程上同步调用 Lua 全局函数。 |

## Overview

ScriptVM is the RAII wrapper around a Lua `lua_State`. It provides the foundation for all Lua scripting in the engine, including script execution, C function registration, and a custom pointer store.

## Module

C++ API via `ScriptVM` class. The Lua-facing APIs are registered through this class (see individual API modules for Lua-level documentation).

## ScriptVM Methods (C++)

### Construction

```
ScriptVM()              — Creates a new Lua state with standard libraries
~ScriptVM()             — Closes the Lua state
```

Move-only (non-copyable).

### Script Lifecycle

#### `InitScript()`

Calls the global Lua function `init()` if it exists. Called once after all C APIs are exported.

#### `UpdateScript()`

Calls the global Lua function `update()` if it exists. Called once per frame.

#### `DestroyScript()`

Calls the global Lua function `destroy()` if it exists. Called during engine shutdown.

### Script Execution

#### `DoString(script [, chunk_name [, error_out [, result_out]]])`

Executes a Lua string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `script` | `string_view` | Lua source code |
| `chunk_name` | `string_view` | Name for error messages (default: "string") |
| `error_out` | `string*` | Receives error message on failure (optional) |
| `result_out` | `string*` | Receives result string via `lua_tostring` (optional) |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `bool` | `true` on success |

#### `DoFile(filename [, error_out])`

Executes a Lua file.

| Parameter | Type | Description |
|-----------|------|-------------|
| `filename` | `string` | Path to .lua file |
| `error_out` | `string*` | Receives error message on failure (optional) |

| Returns | Type | Description |
|---------|------|-------------|
| `ok` | `bool` | `true` on success |

#### `DoDirectory(dir_path)`

Executes all `.lua` files found directly in a directory (non-recursive).

| Returns | Type | Description |
|---------|------|-------------|
| `failed` | `size_t` | Number of files that failed |

### C Function / Module Registration

#### `RegisterFunction(name, func)`

Registers a single C function as a Lua global.

#### `RegisterFunctions(functions)`

Registers a `luaL_Reg` array (terminated by `{NULL, NULL}`) as individual globals.

#### `RegisterModule(name, functions)`

Registers a `luaL_Reg` array as a named module table (global).

#### `RegisterModuleOpen(name, openf [, make_global])`

Registers a module via `luaL_requiref` style open function. The module is stored in `package.preload[name]` for on-demand loading via `require()`. If `make_global` is true (default), it is also set as a global.

#### `RegisterCallback(name, callback)`

Registers a `std::function<int(lua_State*)>` as a global Lua function. The callback follows Lua C calling convention.

### Custom Pointer Store

A per-VM void* array for associating C++ pointers with Lua state.

| Method | Description |
|--------|-------------|
| `ReserveCustomPtrSlots(n)` | Pre-allocate capacity for N pointers |
| `SetCustomPtr(index, ptr)` | Store pointer at 1-based index |
| `GetCustomPtr(index)` | Get pointer at 1-based index |
| `GetCustomPtrAs<T>(index)` | Typed variant of GetCustomPtr |
| `PushCustomPtr(ptr)` | Append pointer, returns new index |
| `SetNullCustomPtr(index)` | Set slot to nullptr |
| `ClearCustomPtrs()` | Drop all stored pointers |
| `CustomPtrCount()` | Number of stored pointers |
| `CustomPtrCapacity()` | Current allocated capacity |
| `HasCustomPtr(index)` | Check if slot has non-null pointer |
| `FindCustomPtr(ptr)` | Find 1-based index of a pointer (-1 if not found) |
| `ContainsCustomPtr(ptr)` | Check if pointer exists in array |
| `CopyCustomPtrsTo(dst, max)` | Copy pointers to external array |
| `CopyCustomPtrsFrom(src, count)` | Replace entire array |

### Utilities

| Method | Description |
|--------|-------------|
| `GetState()` | Returns the raw `lua_State*` |
| `ToString([index])` | Pop stack top and return as string |
| `LuaVersion()` | Returns Lua version string (static) |
| `SetGlobal<T>(name, value)` | Template setter for globals (int, double, string, bool) |
| `GetImporter()` | Returns the ScriptImporter reference |
| `SetImportPath(dir)` | Sets the Lua script search path |

## Lua Lifecycle Functions

Lua scripts can optionally define these global functions:

```lua
function init()
    -- Called once after all C APIs are exported
end

function update()
    -- Called once per frame
end

function destroy()
    -- Called during engine shutdown
end
```
