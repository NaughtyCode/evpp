# BSON Module API (`bson`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程）。所有 BSON 操作为纯 CPU 计算（序列化/反序列化/迭代），无 I/O、无网络访问。函数在调用线程上通过 libbson C API 同步执行。 |
| **线程安全** | 部分。每个 userdata 持有独立的 libbson C 对象（`bson_t*`、`bson_iter_t` 等），多个线程可同时操作**不同的** userdata。但单个 userdata **不可**跨线程共享——libbson 对象本身非线程安全。 |
| **回调线程** | 无回调。所有函数均为同步调用，立即返回。`iter:visit(callback)` 中的 callback 在调用线程上同步执行。 |

## Overview

The `bson` module provides Lua bindings for BSON data manipulation, matching the libbson C library API. All types are registered under the `bson` global table.

## Module

`bson`

---

## bson.Document

**Metatable:** `"bson.document"`

Represents a BSON document. Created via `bson.Document.new()` or `bson.Document.new_from_json(json)`.

### Static Functions

#### `bson.Document.new()`

Creates an empty BSON document.

| Returns | Type | Description |
|---------|------|-------------|
| `doc` | `userdata` | New BSON document |

#### `bson.Document.new_from_json(json)`

Creates a BSON document from a JSON string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `json` | `string` | JSON-encoded document |

| Returns | Type | Description |
|---------|------|-------------|
| `doc` | `userdata` | Parsed BSON document |

### Instance Methods

| Method | Description |
|--------|-------------|
| `doc:to_json()` → `string` | Serialize document to canonical JSON |
| `doc:to_relaxed_json()` → `string` | Serialize to relaxed extended JSON |
| `doc:to_canonical_json()` → `string` | Serialize to canonical extended JSON |
| `doc:get_data()` → `string` | Raw BSON binary data |
| `doc:append(key, value)` | Set a field by key name |
| `doc:append_int32(key, n)` | Append int32 field |
| `doc:append_int64(key, n)` | Append int64 field |
| `doc:append_double(key, n)` | Append double field |
| `doc:append_bool(key, b)` | Append boolean field |
| `doc:append_string(key, s)` | Append string field |
| `doc:append_oid(key, oid)` | Append ObjectId field |
| `doc:append_datetime(key, ms)` | Append datetime (ms since epoch) |
| `doc:append_null(key)` | Append null field |
| `doc:append_document(key, doc)` | Append sub-document |
| `doc:append_array(key, doc)` | Append array (as sub-document) |
| `doc:append_binary(key, subtype, data)` | Append binary data |
| `doc:append_regex(key, pattern, flags)` | Append regex |

### __eq

Two documents can be compared with `==` for structural equality.

---

## bson.Iter

**Metatable:** `"bson.iter"`

Iterates over BSON document fields.

### Static Functions

#### `bson.Iter.new(doc)`

Creates an iterator over a document's fields.

| Parameter | Type | Description |
|-----------|------|-------------|
| `doc` | `userdata` | BSON document to iterate |

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `iter:next()` | `string, value` or `nil` | Advance to next field, returns key and value |
| `iter:key()` | `string` | Current field key |
| `iter:type()` | `string` | Current field BSON type name |
| `iter:type_code()` | `integer` | Current field BSON type code |
| `iter:value()` | `any` | Current field value |
| `iter:value_int32()` | `integer` | Value as int32 |
| `iter:value_int64()` | `integer` | Value as int64 |
| `iter:value_double()` | `number` | Value as double |
| `iter:value_bool()` | `boolean` | Value as boolean |
| `iter:value_string()` | `string` | Value as UTF-8 string |
| `iter:value_oid()` | `userdata` | Value as ObjectId |
| `iter:value_datetime()` | `integer` | Value as datetime (ms) |
| `iter:value_document()` | `userdata` | Value as sub-document |
| `iter:value_array()` | `userdata` | Value as array |
| `iter:value_binary()` | `(integer, string)` | Value as binary (subtype, data) |
| `iter:value_regex()` | `(string, string)` | Value as regex (pattern, flags) |
| `iter:find(key)` | `boolean` | Find a field by key name |
| `iter:visit(callback)` | — | Visit all fields with a callback `function(key, type_name, value)` |

---

## bson.Oid

**Metatable:** `"bson.oid"`

BSON ObjectId (12-byte identifier).

### Static Functions

#### `bson.Oid.new([hex_string])`

Creates a new ObjectId. If no argument is given, a new random ObjectId is generated.

| Parameter | Type | Description |
|-----------|------|-------------|
| `hex_string` | `string` | 24-character hex string (optional) |

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `oid:to_string()` | `string` | 24-character hex representation |
| `oid:get_timestamp()` | `integer` | Unix timestamp from the Oid |

### __eq / __tostring

Supports equality comparison and auto-conversion to hex string.

---

## bson.ArrayBuilder

**Metatable:** `"bson.array_builder"`

Builds BSON arrays efficiently.

### Static Functions

#### `bson.ArrayBuilder.new()`

Creates a new empty array builder.

### Instance Methods

| Method | Description |
|--------|-------------|
| `b:append_int32(n)` | Append int32 value |
| `b:append_int64(n)` | Append int64 value |
| `b:append_double(n)` | Append double value |
| `b:append_bool(v)` | Append boolean value |
| `b:append_string(s)` | Append string value |
| `b:append_oid(oid)` | Append ObjectId |
| `b:append_null()` | Append null |
| `b:append_document(doc)` | Append sub-document |
| `b:append_array(doc)` | Append sub-array |
| `b:append_binary(subtype, data)` | Append binary |
| `b:append_regex(pattern, flags)` | Append regex |
| `b:append_datetime(ms)` | Append datetime |
| `b:build()` → `userdata` | Build and return the BSON array document |

---

## bson.Context

**Metatable:** `"bson.context"`

BSON memory context for custom allocators.

### Static Functions

#### `bson.Context.new()`

Creates a new BSON context.

---

## bson.String

**Metatable:** `"bson.string"`

BSON string wrapper.

---

## bson.JsonReader

**Metatable:** `"bson.json_reader"`

Reads BSON from JSON text.

#### `bson.JsonReader.new(json)` → `userdata`

| Method | Description |
|--------|-------------|
| `reader:read()` → `userdata` or `nil` | Read next BSON document |

---

## bson.JsonDataReader

**Metatable:** `"bson.json_data_reader"`

Reads BSON from JSON data with type annotations.

---

## bson.Reader

**Metatable:** `"bson.reader"`

Reads BSON from raw binary data.

#### `bson.Reader.new(data)` → `userdata`

| Method | Description |
|--------|-------------|
| `reader:read()` → `(doc, bytes_read)` or `nil` | Read next BSON document |

---

## bson.Writer

**Metatable:** `"bson.writer"`

Writes BSON documents to a buffer.

#### `bson.Writer.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `writer:begin()` | Start a new document |
| `writer:end()` | Finish the current document |
| `writer:get_data()` → `string` | Get accumulated BSON data |
| `writer:rollback()` | Discard the current document |

---

## bson.JsonOpts

**Metatable:** `"bson.json_opts"`

JSON serialization options.

#### `bson.JsonOpts.new()` → `userdata`

---

## bson.Value

**Metatable:** `"bson.value"`

Generic BSON value holder.

#### `bson.Value.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `v:copy_to(doc, key)` | Copy value to a document field |
| `v:destroy()` | Release the value |

---

## BSON Vector Types

### bson.VectorInt8ConstView

**Metatable:** `"bson.vector_int8_const_view"`

Read-only view of BSON binary int8 vector data.

### bson.VectorInt8View

**Metatable:** `"bson.vector_int8_view"`

Mutable view of BSON binary int8 vector data.

### bson.VectorFloat32ConstView

**Metatable:** `"bson.vector_float32_const_view"`

Read-only view of float32 binary vector data.

### bson.VectorFloat32View

**Metatable:** `"bson.vector_float32_view"`

Mutable view of float32 binary vector data.

### bson.VectorPackedBitConstView

**Metatable:** `"bson.vector_packed_bit_const_view"`

Read-only view of packed-bit binary vector data.

### bson.VectorPackedBitView

**Metatable:** `"bson.vector_packed_bit_view"`

Mutable view of packed-bit binary vector data.

---

## bson.Ext

Extended BSON utility functions.

| Function | Description |
|----------|-------------|
| `bson.Ext.init_from_json(doc, json)` → `bool` | Parse JSON into an existing document |

---

## 类型详述

BSON 模块中所有对象均为 Lua full userdata，内部持有 libbson C 结构体指针。下表列出常用参数/返回值与底层 C 类型的对应关系：

| 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取/推送方式 |
|-----------|----------|------------|-------------|
| Document | `userdata` | `bson_t*` (full userdata, `__gc` 调用 `bson_destroy`) | `lua_newuserdata` + metatable |
| Iter | `userdata` | `bson_iter_t*` (full userdata, `__gc` 释放) | `lua_newuserdata` + metatable |
| Oid | `userdata` | `bson_oid_t*` (full userdata) | `lua_newuserdata` + metatable |
| ArrayBuilder | `userdata` | `bson_t*` (内部累积) | `lua_newuserdata` + metatable |
| 所有 `append_*` / `new` / `new_from_json` 的 key | `string` | `const char*` | `luaL_checkstring` |
| `append_*` 的 value (int32/int64/double/bool/string/oid) | 按类型 | `int32_t` / `int64_t` / `double` / `bool` / `const char*` / `bson_oid_t*` | `luaL_checkinteger` / `luaL_checknumber` / `lua_toboolean` / `luaL_checkstring` / userdata check |
| `to_json` / `to_canonical_json` / `to_relaxed_json` 返回值 | `string` | `char*` (bson_as_json 分配) → `lua_pushstring` | `bson_strfreev` 释放 |
| `get_data` 返回值 | `string` | `const uint8_t*` + `size_t` | `lua_pushlstring` |
| `iter:next()` 返回值 (key) | `string` | `const char*` (bson_iter_key) | `lua_pushstring` |
| `iter:next()` 返回值 (value) | 按 BSON 类型 | 对应 libbson 类型 | 通过类型分发放到栈上 |
| `iter:type()` 返回值 | `string` | `const char*` (bson_type_to_string) | `lua_pushstring` |
| `oid:to_string()` 返回值 | `string` | `char[25]` (bson_oid_to_string) | `lua_pushstring` |
| Reader/Writer/JsonReader 等 | `userdata` | `bson_reader_t*` / `bson_writer_t*` / `bson_json_reader_t*` 等 | `lua_newuserdata` + metatable |

## Example

```lua
-- Create a document
local doc = bson.Document.new()
doc:append_string("name", "player1")
doc:append_int32("score", 500)
doc:append_bool("active", true)

-- Sub-document
local sub = bson.Document.new()
sub:append_int32("x", 100)
sub:append_int32("y", 200)
doc:append_document("position", sub)

-- Serialize
local json = doc:to_json()
log_info("BSON document: " .. json)

-- Parse from JSON
local doc2 = bson.Document.new_from_json('{"name":"player2","score":300}')

-- Iterate
local iter = bson.Iter.new(doc)
while true do
    local key, val = iter:next()
    if not key then break end
    log_info(key .. " = " .. tostring(val))
end

-- ObjectId
local oid = bson.Oid.new()
log_info("Generated Oid: " .. oid:to_string())

-- Array builder
local arr = bson.ArrayBuilder.new()
arr:append_string("a")
arr:append_string("b")
arr:append_int32(42)
local array_doc = arr:build()
```
