# Lua JSON API

The engine exports the Glaze-backed JSON binding as two global Lua modules:

- `json`: raises a Lua error when a binding operation fails.
- `json_safe`: exposes the same functions, but returns `nil, err` for raised binding errors.

The binding is registered in all engine script VMs that run project Lua code:

- Main script VM through `script::ExportAll`.
- Data-service thread VM in `DBThread::EventLoop`.
- Physics script VM in `PhysicsSystem::Initialize`.

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

Decoded arrays and objects are tagged so empty arrays remain `[]` and empty objects
remain `{}` when encoded again. Plain dense Lua tables are encoded as arrays. Plain
empty or keyed Lua tables are encoded as objects.

## Options

Most functions accept an optional options table.

| Option | Applies to | Meaning |
|--------|------------|---------|
| `comments = true` | `decode`, `parse`, `load`, `read`, `read_file`, `validate`, `minify`, `prettify` | Parse JSONC comments. |
| `pretty = true` | `encode`, `stringify`, `save`, `write`, `write_file` | Write pretty formatted JSON. |
| `prettify = true` | `encode`, `stringify`, `save`, `write`, `write_file` | Alias for `pretty`. |

## Parsing

```lua
local value = json.decode('{"name":"evpp","items":[1,null]}')
local same = json.parse('{"ok":true}')

assert(value.name == "evpp")
assert(value.items[2] == json.null)
assert(json.type(value.items) == "array")
```

`json.decode(text [, opts])` and `json.parse(text [, opts])` parse JSON text and
return the Lua value. Passing `{ comments = true }` enables JSONC input.

## Encoding

```lua
local payload = json.object({
    name = "evpp",
    items = json.array(1, nil, 3),
    empty_array = json.array(),
    empty_object = json.object(),
    missing = json.null,
})

local text = json.encode(payload, { pretty = true })
```

`json.encode(value [, opts])` and `json.stringify(value [, opts])` convert a Lua
value to JSON text. `nil` values inside `json.array(...)` are preserved as JSON
`null`; regular Lua table fields with `nil` are still absent because Lua does not
store nil table entries.

Encoding rejects unsupported Lua values, non-finite numbers, table cycles, and
marked arrays that are not dense positive integer sequences.

## Files

```lua
json.save("logs/state.json", payload, { pretty = true })
local loaded = json.load("logs/state.json")
```

`json.load(path [, opts])`, `json.read(path [, opts])`, and
`json.read_file(path [, opts])` read a JSON file and parse it.

`json.save(path, value [, opts])`, `json.write(path, value [, opts])`, and
`json.write_file(path, value [, opts])` encode a value and write it to disk. Parent
directories are created when needed. On success these functions return `true`.

## Validation And Formatting

```lua
local ok, err = json.validate(text)
local compact = json.minify(text)
local pretty = json.prettify(compact)
```

`json.validate(text [, opts])` returns `true` for valid input. For invalid input it
returns `false, err` without raising.

`json.minify(text [, opts])` validates and compacts JSON text.

`json.prettify(text [, opts])` validates and formats JSON text.

## Shape Helpers

```lua
local arr = json.array("a", nil, "c")
local obj = json.object({ name = "evpp" })

json.as_array(existing_table)
json.as_object(existing_table)
```

`json.array(...)` creates a table tagged as a JSON array. Nil varargs are converted
to `json.null`.

`json.object([table])` creates or tags a table as a JSON object.

`json.as_array(table)` and `json.as_object(table)` tag an existing table and return
the same table.

## Type Helpers

```lua
json.type(value)       -- "null", "boolean", "string", "number", "array", "object", or Lua type name
json.is_null(value)
json.is_array(value)
json.is_object(value)
```

`json.is_null(nil)` is `true` because nil maps to JSON null when passed as a value.

## Safe API

```lua
local value, err = json_safe.decode(text)
if not value then
    log_error(err)
end
```

`json_safe` wraps the throwing API with `pcall`. Binding errors return `nil, err`
instead of raising. Functions that do not raise keep their normal result shape, so
`json_safe.validate("{")` still returns `false, err`.
