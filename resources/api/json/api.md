# JSON System API

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程。所有 `json.*` / `json_safe.*` 函数都是同步 CPU/文件操作；文件读写在调用线程执行。 |
| **线程安全** | JSON 转换本身无共享可变状态；Lua table、metatable 和 `lua_State` 仍必须只在 VM 所属线程访问。 |
| **回调线程** | 无回调。 |

## Overview

The JSON system exposes Glaze-backed JSON parsing, encoding, validation, formatting, and file helpers through two global modules:

- `json`: raises a Lua error when a binding operation fails.
- `json_safe`: exposes the same functions, but catches raised binding errors and returns `nil, errmsg`.

## Data Model

| JSON value | Lua representation |
|------------|--------------------|
| `null` | `json.null` lightuserdata sentinel |
| boolean | Lua boolean |
| string | Lua string |
| integer | Lua integer |
| number | Lua number |
| array | Lua table tagged as a JSON array |
| object | Lua table tagged as a JSON object |

Decoded arrays and objects are tagged so empty arrays remain `[]` and empty objects remain `{}` when encoded again. Plain dense Lua tables are encoded as arrays; plain empty or keyed tables are encoded as objects.

## Options

Most functions accept an optional options table.

| Option | Applies to | Meaning |
|--------|------------|---------|
| `comments = true` | `decode`, `parse`, `load`, `read`, `read_file`, `validate`, `minify`, `prettify` | Parse JSONC comments. |
| `pretty = true` | `encode`, `stringify`, `save`, `write`, `write_file` | Write pretty formatted JSON. |
| `prettify = true` | `encode`, `stringify`, `save`, `write`, `write_file` | Alias for `pretty`. |

## Functions

### `json.decode(text [, opts])`
### `json.parse(text [, opts])`

Parses JSON/JSONC text and returns the Lua value.

### `json.encode(value [, opts])`
### `json.stringify(value [, opts])`

Converts a Lua value to JSON text. Unsupported Lua values, non-finite numbers, table cycles, and marked arrays with sparse/non-positive keys raise an error.

### `json.load(path [, opts])`
### `json.read(path [, opts])`
### `json.read_file(path [, opts])`

Reads a JSON file and parses it.

### `json.save(path, value [, opts])`
### `json.write(path, value [, opts])`
### `json.write_file(path, value [, opts])`

Encodes a value and writes it to disk. Parent directories are created when needed. Returns `true` on success.

### `json.validate(text [, opts])`

Returns `true` for valid input. For invalid input it returns `false, errmsg` without raising.

### `json.minify(text [, opts])`

Validates and compacts JSON/JSONC text.

### `json.prettify(text [, opts])`

Validates and formats JSON/JSONC text.

### `json.array(...)`

Creates a table tagged as a JSON array. Nil varargs are stored as `json.null` so they encode as JSON `null`.

### `json.object([table])`

Creates a new empty object table or tags the supplied table as a JSON object.

### `json.as_array(table)`
### `json.as_object(table)`

Tags an existing table and returns the same table.

### `json.is_array(value)`
### `json.is_object(value)`
### `json.is_null(value)`
### `json.type(value)`

Shape/type helpers. `json.type(value)` returns `"null"`, `"boolean"`, `"string"`, `"number"`, `"array"`, `"object"`, or the Lua type name. `json.is_null(nil)` returns `true`.

## Safe API

`json_safe` contains the same function names as `json`. Functions that raise in `json` return `nil, errmsg` in `json_safe`. Functions that already return status values keep their normal shape, so `json_safe.validate("{")` still returns `false, errmsg`.

## Example

```lua
local payload = json.object({
    name = "evpp",
    values = json.array(1, nil, 3),
    empty = json.array(),
    missing = json.null,
})

local text = json.encode(payload, { pretty = true })
local roundtrip = json.decode(text)
assert(json.type(roundtrip.values) == "array")

local ok, err = json.validate("{", { comments = true })
assert(ok == false and err)

local value, safe_err = json_safe.decode("{")
assert(value == nil and safe_err)
```

## Module Metadata

Both modules expose:

- `_NAME = "json"`
- `_VERSION = "glaze-json-lua 1.0.0"`
- `_DESCRIPTION = "Lua bindings for Glaze JSON"`
- `null` JSON null sentinel
