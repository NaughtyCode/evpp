# Glaze JSON Lua Bindings

**Date**: 2026-05-31
**Scope**: Lua scripting runtime, Glaze JSON bridge, unit tests

## Summary

Added first-class Lua bindings for the JSON APIs provided by
`src/thirdparty/glaze`. Scripts can now parse, validate, format, encode,
load, and save JSON data through the `json` module, with `json_safe`
offering the same API in nil-plus-error form.

The same bindings are now exported for the main VM, data-service thread VM,
and physics-thread VM.

## API

- `json.decode` / `json.parse` parse JSON text into Lua values.
- `json.encode` / `json.stringify` serialize Lua values through Glaze.
- `json.load` / `json.read_file` parse JSON from disk.
- `json.save` / `json.write_file` serialize JSON to disk.
- `json.validate`, `json.minify`, and `json.prettify` expose validation and formatting.
- `json.null` preserves JSON null in arrays and objects.
- `json.array`, `json.object`, `json.as_array`, and `json.as_object` preserve empty container shape.
- `json.is_array`, `json.is_object`, `json.is_null`, and `json.type` expose JSON-oriented type checks.

## Behavior

- Decoded JSON arrays and objects are tagged with internal Lua metatables so empty arrays round trip as `[]` and empty objects round trip as `{}`.
- Lua `nil` encodes as JSON `null` at top level; `json.array(...)` preserves nil varargs as `json.null`.
- Lua tables are encoded as arrays only when they are dense positive integer sequences or explicitly marked with `json.as_array`.
- JSONC input is supported for parse/validate/format paths via `{comments = true}`.
- Pretty output is supported for encode/save through `{pretty = true}` or `{prettify = true}`.
- Circular tables, sparse marked arrays, unsupported Lua values, duplicate object keys after conversion, and non-finite numbers are rejected with explicit errors.
- `DBThread::EventLoop()` registers `json` / `json_safe` before data-service scripts are loaded.
- `PhysicsSystem::Initialize()` registers `json` / `json_safe` before physics scripts are loaded.

## Tests

Added `unit.json_bind` covering module export, parse/encode round trips,
null preservation, array/object shape, validation, minify/prettify, JSONC,
safe wrappers, file load/save, circular table rejection, and sparse marked
array rejection. Added DBScriptVM coverage to ensure the data-service VM can
host the same JSON module.
