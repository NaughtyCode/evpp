# MessagePack API

Last synced: 2026-06-05.

The engine exports MessagePack serialization through two global Lua modules:

- `cmsgpack`: raw API that raises Lua errors on binding failures.
- `cmsgpack_safe`: same function names, wrapped so raised errors return
  `nil, errmsg`.

Both modules are exported by `ExportMsgPack`. The main runtime exports them
through `MainThreadScriptVM::ExportRuntimeBindings()`.

## Threading

MessagePack functions are synchronous and execute on the calling Lua VM thread.
They do CPU-only encode/decode work, but they read current MessagePack limits
from server config, so callers should still follow the normal Lua VM
owner-thread rule.

## Data Model

| Lua value | MessagePack value |
|-----------|-------------------|
| `nil` | nil |
| unsupported Lua type | nil |
| boolean | bool |
| string | str |
| integer | int or uint |
| non-integer number | float32 or float64 |
| dense table with keys `1..N` | array |
| other table | map |

A table is treated as an array only when all keys are positive sequential
integers from `1` to `N`. Other tables are encoded as maps.

## Functions

### `cmsgpack.pack(...)`

Encodes one or more Lua values and concatenates their MessagePack encodings into
one binary string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `...` | any | One or more values to encode. |

| Returns | Type | Description |
|---------|------|-------------|
| `data` | binary string | Concatenated MessagePack bytes. |

Encoding rejects:

- zero arguments;
- tables deeper than `server.msgpack.max_nesting_depth`;
- encoded payloads larger than `server.msgpack.max_payload_size`;
- arrays or maps too large for the supported MessagePack size fields;
- Lua stack overflows while traversing tables.

### `cmsgpack.unpack(data)`

Decodes all MessagePack values from `data`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | binary string | MessagePack bytes. |

| Returns | Type | Description |
|---------|------|-------------|
| `...` | any | All decoded values. |

### `cmsgpack.unpack_one(data [, offset])`

Decodes one value from `data` starting at byte offset `offset`, defaulting to
`0`.

| Returns | Type | Description |
|---------|------|-------------|
| `value` | any | Decoded value. |
| `next_offset` | integer | Next byte offset, or `-1` when the end is reached. |

### `cmsgpack.unpack_limit(data, limit [, offset])`

Decodes up to `limit` values from `data` starting at byte offset `offset`,
defaulting to `0`.

| Returns | Type | Description |
|---------|------|-------------|
| `...` | any | Up to `limit` decoded values. |
| `next_offset` | integer | Next byte offset, or `-1` when the end is reached. |

Decode APIs reject:

- negative `limit` or `offset`;
- `offset` beyond input length;
- payloads larger than `server.msgpack.max_payload_size`;
- incomplete input bytes;
- malformed MessagePack input;
- arrays or maps deeper than `server.msgpack.max_nesting_depth`.

## Safe API

`cmsgpack_safe` exposes:

```lua
cmsgpack_safe.pack(...)
cmsgpack_safe.unpack(data)
cmsgpack_safe.unpack_one(data [, offset])
cmsgpack_safe.unpack_limit(data, limit [, offset])
```

On success, safe functions keep the wrapped function's normal return shape. On
raised binding errors, they return:

```lua
nil, errmsg
```

## Configuration

| Config path | Meaning |
|-------------|---------|
| `server.msgpack.max_nesting_depth` | Maximum table nesting depth accepted by encode/decode. |
| `server.msgpack.max_payload_size` | Maximum encoded or decoded payload size in bytes. |

## Examples

```lua
local packed = cmsgpack.pack("hello", 42, true, { 1, 2, 3 })
local a, b, c, d = cmsgpack.unpack(packed)

local value, offset = cmsgpack.unpack_one(packed)
while offset ~= -1 do
    value, offset = cmsgpack.unpack_one(packed, offset)
end

local value, next_or_err = cmsgpack_safe.unpack_one("\xd9")
if value == nil and type(next_or_err) == "string" then
    log_error("unpack failed: " .. next_or_err)
end
```

## Module Metadata

Both modules expose:

| Field | Value |
|-------|-------|
| `_NAME` | `cmsgpack` |
| `_VERSION` | `lua-cmsgpack 0.4.0` |
| `_COPYRIGHT` | `Copyright (C) 2012, Salvatore Sanfilippo` |
| `_DESCRIPTION` | `MessagePack C implementation for Lua` |
