# Import System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 主线程（EventLoop 线程）。文件 I/O 在调用线程上同步执行。 |
| **线程安全** | 否。`ScriptImporter` 存储在 Lua registry 中，每个 ScriptVM 独立。不可跨 VM 或跨线程共享。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。`import(module_name)` 会同步加载并执行 Lua 文件。 |

## Overview

The import system provides a custom module loader for Lua scripts. It replaces Lua's built-in `require` with a callable table `import` that supports path configuration, cache inspection, and cache clearing.

## Global

`import` — a callable table (not a plain function)

## Functions

### `import(module_name)`

Loads and executes a Lua module by name. Uses the configured search paths and caches loaded modules.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `module_name` | `string` | `std::string_view` (via `luaL_checklstring`) | Module name to load (must not be empty) |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `result` | `any` | Lua stack value(s) | Whatever the module script returns（由被加载模块的 return 语句决定） |

### `import.setpath(paths)`

Sets the module search paths, replacing any existing paths.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `paths` | `string` | `std::string` (via `luaL_checklstring`) | Semicolon-separated directory paths |

| Returns | — | 无返回值 |

### `import.addpath(path)`

Appends a single path to the existing search paths.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `path` | `string` | `std::string` (via `luaL_checklstring`) | Directory path to add |

| Returns | — | 无返回值 |

### `import.loaded()`

Returns a shallow copy of the loaded modules table.

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `modules` | `table` | Lua table (shallow copy via `lua_next`) | Shallow copy of `package.loaded`。键为模块名字符串，值为模块返回值。 |

### `import.clearcache()`

Clears the module cache, forcing subsequent imports to reload from disk.

| Returns | — | 无返回值 |

## 类型详述

| 函数 | 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取方式 |
|------|-----------|----------|------------|---------|
| `import(module_name)` | module_name | `string` | `std::string_view` | `luaL_checklstring` → C string + length |
| `import.setpath(paths)` | paths | `string` | `std::string` | `luaL_checklstring` → `std::string(paths, len)` |
| `import.addpath(path)` | path | `string` | `std::string` | `luaL_checklstring` → `std::string(path, len)` |
| `import.loaded()` | modules | `table` | — | `lua_next` 遍历 `package.loaded` 构建浅拷贝 |

## Example

```lua
-- Configure search paths
import.setpath("resources/script/server;resources/script/shared")

-- Add an additional path
import.addpath("resources/script/lib")

-- Import a module
local mymod = import("mymodule")
mymod.do_something()

-- Check what's loaded
local loaded = import.loaded()
for k, _ in pairs(loaded) do
    log_debug("loaded: " .. k)
end

-- Reload a module during development
import.clearcache()
local mymod_reloaded = import("mymodule")
```

## Notes

- The import system is backed by `ScriptImporter`, stored in the Lua registry per-VM
- It uses Lua's `package.loaded` for caching, making it compatible with `require`
- Paths use platform-specific separators; on Windows, semicolons separate paths
