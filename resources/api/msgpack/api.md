# MessagePack System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程 / EventLoop 线程）。所有 pack/unpack 函数均为纯 CPU 计算，无 I/O、无状态。 |
| **线程安全** | 是（无共享状态）。函数内部仅在栈上分配缓冲区（`EncodeBuf`/`DecodeCursor`），不访问任何全局或静态可变状态。多个 VM 或多个线程可同时调用。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。 |

## Overview

The MessagePack system provides binary serialization/deserialization via two global modules: `cmsgpack` and `cmsgpack_safe`. Both modules have identical APIs; the `_safe` variant wraps every call in `pcall` so errors return `(nil, errmsg)` instead of raising.

## Modules

- `cmsgpack` — raw API, raises Lua errors on failure
- `cmsgpack_safe` — safe API, returns `nil, errmsg` on failure

## Functions

### `cmsgpack.pack(...)`

Encodes one or more Lua values into a MessagePack binary string.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `...` | `any` (variadic) | Lua stack values | One or more Lua values to encode。每个值依次编码后拼接。 |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `data` | `string` | `lua_pushlstring` | Concatenated MessagePack-encoded binary data |

**Type mapping (Lua → MessagePack):**

| Lua Type | 检测方式 | MessagePack Type |
|----------|----------|-----------------|
| `string` | `lua_type(L, -1) == LUA_TSTRING` → `lua_tolstring` | str (fixstr/str8/str16/str32) |
| `boolean` | `lua_type(L, -1) == LUA_TBOOLEAN` → `lua_toboolean` | bool (0xc3 true / 0xc2 false) |
| `integer` | `lua_isinteger(L, -1)` → `lua_tointeger` | int (fixint/int8/16/32/64, uint8/16/32/64) |
| `number` (float) | `lua_isnumber` + `!lua_isinteger` → `lua_tonumber` | float32 (if exact as float) or float64 |
| `table` (array-like) | `TableIsArray()` → `lua_rawlen` | array (fixarray/array16/array32) |
| `table` (map-like) | `!TableIsArray()` → `lua_next` | map (fixmap/map16/map32) |
| `nil` / other | `default` branch | nil (0xc0) |

A table is considered "array-like" when it has sequential integer keys `1..N` where `N` equals the key count. Otherwise it is encoded as a map.

### `cmsgpack.unpack(data)`

Decodes all MessagePack values from a binary string.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` (via `luaL_checklstring`) | MessagePack-encoded binary data |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `...` | `any` (variadic) | Lua stack values | All decoded Lua values。等价于 `unpack_limit(data, INT_MAX)`。 |

### `cmsgpack.unpack_one(data [, offset])`

Decodes a single MessagePack value starting at a byte offset.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` | MessagePack-encoded binary data |
| `offset` | `integer` | `int` (via `luaL_optinteger`, default 0) | Byte offset to start decoding from |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `value` | `any` | Lua stack value | The decoded Lua value |
| `next_offset` | `integer` | `lua_Integer` | Byte offset of the next value, or `-1` if at end |

### `cmsgpack.unpack_limit(data, limit [, offset])`

Decodes up to `limit` values starting at a byte offset.

| Parameter | Type | C Type | Description |
|-----------|------|--------|-------------|
| `data` | `string` | `const char*` + `size_t` | MessagePack-encoded binary data |
| `limit` | `integer` | `int` (via `luaL_checkinteger`) | Maximum number of values to decode |
| `offset` | `integer` | `int` (via `luaL_optinteger`, default 0) | Byte offset to start decoding from |

| Returns | Type | C Type | Description |
|---------|------|--------|-------------|
| `...` | `any` (variadic) | Lua stack values | Up to `limit` decoded values |
| `next_offset` | `integer` | `lua_Integer` | Byte offset after the last decoded value, or `-1` if at end |

## 返回值数量

| 函数 | 返回值数量 | 说明 |
|------|-----------|------|
| `pack(...)` | 1 | 拼接后的二进制字符串 |
| `unpack(data)` | 0..N | 由数据内容决定 |
| `unpack_one(data, [offset])` | 2 | `value, next_offset` |
| `unpack_limit(data, limit, [offset])` | N+1 | N 个解码值 + `next_offset` |

`cmsgpack_safe` 在出错时固定返回 `(nil, errmsg)` (2 个值)。

## Configuration

The maximum nesting depth for nested tables is controlled by `ServerConfig.msgpack.max_nesting_depth`. Tables exceeding this depth are encoded as nil. The default is configured in `config_constants.h`.

## Example

```lua
-- Pack multiple values
local packed = cmsgpack.pack("hello", 42, true, {1, 2, 3})
-- packed is a binary string

-- Unpack all values
local a, b, c, d = cmsgpack.unpack(packed)
-- a = "hello", b = 42, c = true, d = {1, 2, 3}

-- Unpack one value at a time
local val1, off = cmsgpack.unpack_one(packed)
local val2, off = cmsgpack.unpack_one(packed, off)

-- Safe unpacking (no exceptions)
local ok, val = cmsgpack_safe.unpack_one(malformed_data)
if not ok then
    log_error("unpack failed: " .. val)  -- val is error message
end
```

## Module Metadata

Both modules have the following metadata fields:
- `_NAME` = `"cmsgpack"`
- `_VERSION` = `"lua-cmsgpack 0.4.0"`
- `_DESCRIPTION` = `"MessagePack C implementation for Lua"`
