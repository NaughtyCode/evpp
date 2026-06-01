# DB BSON Table Codec API (`db_bson`)

## Overview

`db_bson` is registered into `DBScriptVM` and is also available through
`require("db_bson")`. It converts Lua tables to `bson.doc` userdata and converts
Lua tables or `bson.doc` userdata to Extended JSON strings. It also converts
`bson.doc` userdata back to Lua tables.

The module is intended for script-side database request construction and result
inspection. By default, it keeps common Lua values convenient. When exact BSON
type round-tripping is required, use tagged wrapper values and
`to_table(..., { preserve_types = true })`.

## Module

```lua
local bson = require("db_bson")
```

The same table is also exported as the global `db_bson`.

## Type Mapping

### Lua to BSON

| Lua value | BSON value |
|-----------|------------|
| `boolean` | bool |
| integer | int32 when it fits, otherwise int64 |
| non-integer number | double |
| string | UTF-8 string |
| dense 1-based non-empty table | BSON array |
| other table | BSON document |
| `bson.doc` userdata | nested BSON document |
| `db_bson.null` | null |
| `db_bson.undefined` | undefined |
| `db_bson.min_key` | minKey |
| `db_bson.max_key` | maxKey |
| wrapper table from this module | matching BSON type, including explicit int32/int64/double |

Unsupported Lua values, cyclic tables, sparse arrays, duplicate document keys
after numeric key conversion, invalid UTF-8 text, and invalid BSON keys return
`nil, err` from `to_bson`. Use `db_bson.binary(...)` for arbitrary byte strings.

### BSON to Lua, Default Mode

| BSON value | Lua value |
|------------|-----------|
| bool, int32, int64, double, UTF-8 | matching primitive |
| document | table |
| array | Lua array table |
| binary | string |
| ObjectId | 24-character hex string |
| datetime | integer milliseconds since epoch |
| regex | `{ ["$regex"] = pattern, ["$options"] = options }` |
| DBPointer | `{ ["$ref"] = collection, ["$id"] = oid_hex }` |
| code | string |
| code with scope | `{ ["$code"] = code, ["$scope"] = scope_table }` |
| symbol | string |
| timestamp | `{ timestamp = seconds, increment = increment }` |
| decimal128 | decimal string |
| null | `db_bson.null` |
| undefined | `db_bson.undefined` |
| minKey | `db_bson.min_key` |
| maxKey | `db_bson.max_key` |

### BSON to Lua, Preserve Types Mode

With `preserve_types = true`, BSON scalar types that would otherwise lose
precision or BSON type identity become wrapper tables that can be passed back to
`to_bson` without losing their BSON type. This includes int32, int64, double,
ObjectId, datetime, timestamp, binary, regex, code, symbol, decimal128, and
DBPointer values.

```lua
local t = db_bson.to_table(doc, { preserve_types = true })
local same_doc = db_bson.to_bson(t)
```

Wrapper tables expose readable fields such as `.value`, `.subtype`,
`.timestamp`, `.increment`, `.pattern`, `.options`, `.collection`, and `.oid`.
They also carry internal metadata for `db_bson`; avoid using generic table
serialization on wrapper tables unless those internal fields are ignored.

## Functions

### `db_bson.to_bson(table [, root_as_array_or_options])`

Converts a Lua table to `bson.doc`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `table` | `table` | Source table |
| `root_as_array_or_options` | `boolean` or `table` | Optional root conversion control |

| Returns | Type | Description |
|---------|------|-------------|
| `doc` | `bson.doc` or `nil` | Converted BSON document |
| `err` | `string` or `nil` | Error message on failure |

Options table fields:

| Field | Type | Description |
|-------|------|-------------|
| `root_as_array` | `boolean` | Force root table to BSON array or document |
| `array` | `boolean` | Alias for `root_as_array` |

Explicit `db_bson.array(table)` or `db_bson.document(table)` tags override the
root option.

```lua
local doc, err = db_bson.to_bson({
    name = "player",
    score = 100,
    tags = { "new", "ranked" },
    deleted_at = db_bson.null,
})
```

### `db_bson.from_table(table [, root_as_array_or_options])`

Alias for `db_bson.to_bson`.

### `db_bson.to_table(doc [, root_as_array_or_options])`

Converts a `bson.doc` userdata to a Lua table.

| Parameter | Type | Description |
|-----------|------|-------------|
| `doc` | `bson.doc` | Source BSON document |
| `root_as_array_or_options` | `boolean` or `table` | Optional root conversion control |

| Returns | Type | Description |
|---------|------|-------------|
| `table` | `table` or `nil` | Converted Lua table |
| `err` | `string` or `nil` | Error message on failure |

Options table fields:

| Field | Type | Description |
|-------|------|-------------|
| `root_as_array` | `boolean` | Force root BSON document to Lua array or table |
| `array` | `boolean` | Alias for `root_as_array` |
| `preserve_types` | `boolean` | Return exact BSON scalar types as wrappers |

`bson.doc` userdata created by `db_bson.to_bson` remembers whether its root was
encoded as an array or document, so empty arrays and numeric-key documents round
trip through `db_bson.to_table`/`db_bson.to_json`. Raw `bson.doc` userdata created
by the lower-level `bson` module has no such metadata; pass `true` or
`{ root_as_array = true }` when a raw empty root array must come back as a Lua
array. For nested raw `bson.doc` values, wrap them with `db_bson.array(raw_doc)`
or `db_bson.document(raw_doc)` to force the embedded BSON value shape.

### `db_bson.to_json(value [, root_as_array_or_options])`

Converts a Lua table or `bson.doc` userdata to relaxed Extended JSON. This is a
convenience wrapper around the same validation and table-shape logic used by
`to_bson`, so invalid UTF-8 text, sparse arrays, duplicate keys, and cyclic
tables return `nil, err`.

`value` may be a Lua table or `bson.doc`. The root options are the same as
`to_bson` and `to_table`: pass `true`, `{ root_as_array = true }`, or a
`db_bson.array(...)` wrapper when the root should be emitted as a JSON array.

```lua
local json = assert(db_bson.to_json({
    _id = db_bson.oid("000000000000000000000001"),
    tags = db_bson.array({ "new", "ranked" }),
}))
```

### `db_bson.to_relaxed_json(value [, root_as_array_or_options])`

Alias for `db_bson.to_json`.

### `db_bson.to_canonical_json(value [, root_as_array_or_options])`

Converts a Lua table or `bson.doc` userdata to canonical Extended JSON.

### `db_bson.to_legacy_json(value [, root_as_array_or_options])`

Converts a Lua table or `bson.doc` userdata to legacy Extended JSON.

## Explicit Table Shape

### `db_bson.array([table_or_doc])`

Marks a Lua table or raw `bson.doc` as a BSON array. If no value is passed,
creates a new empty array wrapper.

```lua
local raw = bson.new()
local doc = assert(db_bson.to_bson({
    empty_items = db_bson.array({}),
    items = db_bson.array({ 10, 20 }),
    raw_items = db_bson.array(raw),
}))
```

Lua-table arrays must be dense and 1-based. Sparse arrays fail during `to_bson`.
Raw `bson.doc` array wrappers must already contain dense zero-based BSON array
keys.

### `db_bson.document([table_or_doc])`

Marks a Lua table or raw `bson.doc` as a BSON document. This is useful when
numeric keys should be encoded as document keys rather than array indexes, or
when a raw nested `bson.doc` with numeric keys must not be treated as an array.

```lua
local raw_numeric = bson.new()
bson.append_int32(raw_numeric, "0", 10)
local doc = assert(db_bson.to_bson({
    numeric_keys = db_bson.document({ [1] = "one" }),
    raw_numeric_keys = db_bson.document(raw_numeric),
}))
```

Numeric document keys are converted to strings.
Wrappers around raw `bson.doc` values are shape-only references; do not add Lua
fields to those wrapper tables.

## BSON Scalar Wrappers

Wrapper constructors return a table on success. Validation failures return
`nil, err`. Type mismatches for required arguments follow Lua's `luaL_check*`
behavior and raise a Lua argument error.

### `db_bson.int32(value)`

Creates a BSON int32 wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `integer` | Signed 32-bit integer |

Wrapper fields: `value`.

### `db_bson.int64(value)`

Creates a BSON int64 wrapper. Use this when a value fits in int32 but must be
encoded as BSON int64.

Wrapper fields: `value`.

### `db_bson.double(value)`

Creates a BSON double wrapper. BSON doubles use IEEE 754 semantics, so finite
values, infinities, and NaN are preserved.

Wrapper fields: `value`.

### `db_bson.oid(hex)`

Creates an ObjectId wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `hex` | `string` | 24-character hex ObjectId |

Wrapper fields:

| Field | Type | Description |
|-------|------|-------------|
| `value` | `string` | ObjectId hex string |

### `db_bson.datetime(ms)`

Creates a BSON datetime wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `ms` | `integer` | Milliseconds since Unix epoch |

Wrapper fields: `value`.

### `db_bson.timestamp(timestamp [, increment])`

Creates a BSON timestamp wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `timestamp` | `integer` | Timestamp seconds, 0 to UINT32_MAX |
| `increment` | `integer` | Increment, 0 to UINT32_MAX, default 0 |

Wrapper fields: `timestamp`, `increment`.

### `db_bson.binary(data [, subtype])`

Creates a BSON binary wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `string` | Raw bytes |
| `subtype` | `integer` | BSON binary subtype, 0 to 255, default 0 |

Wrapper fields: `value`, `subtype`.

### `db_bson.regex(pattern [, options])`

Creates a BSON regex wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `pattern` | `string` | Regex pattern, no embedded NUL bytes |
| `options` | `string` | Regex options (`i`, `m`, `x`, `l`, `s`, `u` only), no embedded NUL bytes, default `""` |

Wrapper fields: `pattern`, `options`.

### `db_bson.code(javascript [, scope])`

Creates a BSON code or code-with-scope wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `javascript` | `string` | JavaScript source, no embedded NUL bytes |
| `scope` | `table` or `bson.doc` | Optional code scope document |

Wrapper fields: `value`, optional `scope`.

### `db_bson.symbol(value)`

Creates a BSON symbol wrapper.

Wrapper fields: `value`.

### `db_bson.decimal128(value)`

Creates a BSON decimal128 wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `string` | Decimal128 string accepted by libbson |

Wrapper fields: `value`.

### `db_bson.dbpointer(collection, oid)`

Creates a BSON DBPointer wrapper.

| Parameter | Type | Description |
|-----------|------|-------------|
| `collection` | `string` | Collection namespace, no embedded NUL bytes |
| `oid` | `string` | 24-character hex ObjectId |

Wrapper fields: `collection`, `oid`.

## Sentinel Values

| Value | BSON type |
|-------|-----------|
| `db_bson.null` | null |
| `db_bson.undefined` | undefined |
| `db_bson.min_key` | minKey |
| `db_bson.max_key` | maxKey |

Sentinels are lightuserdata values and should be compared through helper
functions or `db_bson.type`.

### Sentinel Helpers

| Function | Returns |
|----------|---------|
| `db_bson.is_null(value)` | `true` if value is `db_bson.null` |
| `db_bson.is_undefined(value)` | `true` if value is `db_bson.undefined` |
| `db_bson.is_min_key(value)` | `true` if value is `db_bson.min_key` |
| `db_bson.is_max_key(value)` | `true` if value is `db_bson.max_key` |

### `db_bson.type(value)`

Returns a string describing a sentinel, wrapper, or normal Lua value.

Possible BSON-specific results:

`"null"`, `"undefined"`, `"min_key"`, `"max_key"`, `"array"`, `"document"`,
`"int32"`, `"int64"`, `"double"`, `"oid"`, `"datetime"`, `"timestamp"`,
`"binary"`, `"regex"`, `"code"`, `"symbol"`, `"decimal128"`, `"dbpointer"`.

For non-BSON values, returns Lua's type name.

## Examples

### Insert-Ready Document

```lua
local b = require("db_bson")

local doc, err = b.to_bson({
    _id = b.oid("000000000000000000000001"),
    name = "player1",
    score = 500,
    tags = b.array({ "new", "ranked" }),
    created_at = b.datetime(1717200000000),
    deleted_at = b.null,
})

if not doc then
    log_error("BSON conversion failed: " .. tostring(err))
    return
end

local request_json = assert(b.to_json(doc))
```

### Preserve BSON Types

```lua
local b = require("db_bson")

local original = assert(b.to_bson({
    oid = b.oid("000000000000000000000001"),
    price = b.decimal128("123.45"),
    payload = b.binary(string.char(1, 2, 3), 128),
}))

local table_view = assert(b.to_table(original, { preserve_types = true }))
assert(b.type(table_view.oid) == "oid")
assert(table_view.price.value == "123.45")
assert(table_view.payload.subtype == 128)

local round_tripped = assert(b.to_bson(table_view))
```

### Root Array

```lua
local b = require("db_bson")

local array_doc = assert(b.to_bson(b.array({ "a", "b" })))
local array_table = assert(b.to_table(array_doc, true))
assert(array_table[1] == "a")
```

## Notes

- The default array detector treats only dense, 1-based, non-empty Lua tables as
  arrays. Tables that contain only positive integer keys but have holes are
  rejected as implicit sparse arrays. Use `db_bson.array({})` for empty arrays
  and `db_bson.document(...)` when numeric keys should be encoded as document
  keys.
- BSON document keys must be strings or integers, cannot contain embedded NUL
  bytes, must be valid UTF-8, and must be unique after numeric keys are
  converted to strings.
- Plain strings, code, regex patterns, symbols, and DBPointer collection names
  must be valid UTF-8. UTF-8 strings and symbols may contain embedded NUL bytes
  where BSON permits them; BSON cstring fields such as keys, regex pattern,
  regex options, code, and DBPointer collection names may not.
- BSON double values preserve IEEE 754 infinities and NaN.
- `preserve_types = true` is intended for round-tripping; default mode is better
  for simple script-side inspection.
