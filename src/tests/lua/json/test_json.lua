-- test_json.lua
-- Lua-facing coverage for the Glaze JSON binding.

local passed = 0
local failed = 0
local current_test = ""

local function info(message)
    if type(log_info) == "function" then
        log_info(message)
    end
end

local function fail_log(message)
    if type(log_error) == "function" then
        log_error(message)
    else
        error(message, 0)
    end
end

local t = {}

function t.start(name)
    current_test = name
    info("")
    info("=== " .. name .. " ===")
end

function t.assert_truthy(cond, msg)
    if cond then
        passed = passed + 1
        info("  [PASS] " .. (msg or "ok"))
    else
        failed = failed + 1
        fail_log("  [FAIL] " .. current_test .. ": " .. (msg or "assertion failed"))
    end
end

function t.assert_eq(a, b, msg)
    t.assert_truthy(a == b, (msg or "eq") .. " expected [" .. tostring(b) .. "], got [" .. tostring(a) .. "]")
end

function t.assert_contains(haystack, needle, msg)
    local found = type(haystack) == "string" and string.find(haystack, needle, 1, true) ~= nil
    t.assert_truthy(found, msg or ("expected '" .. needle .. "' in '" .. tostring(haystack) .. "'"))
end

function t.summary()
    local total = passed + failed
    info("")
    info("=== " .. total .. " assertion(s): " .. passed .. " passed, " .. failed .. " failed ===")
    if failed > 0 then
        fail_log("Some tests FAILED!")
    else
        info("All tests PASSED.")
    end
    return failed == 0
end

t.start("json modules are exported")
t.assert_eq(type(json), "table", "json module table")
t.assert_eq(type(json_safe), "table", "json_safe module table")
t.assert_eq(type(json.decode), "function", "json.decode exists")
t.assert_eq(type(json.encode), "function", "json.encode exists")
t.assert_eq(type(json.load), "function", "json.load exists")
t.assert_eq(type(json.save), "function", "json.save exists")
t.assert_eq(json.type(json.null), "null", "json.null is JSON null")

t.start("decode preserves JSON shape")
local decoded = json.decode([[{"name":"lua","count":42,"items":[1,true,null],"emptyArray":[],"emptyObject":{}}]])
t.assert_eq(decoded.name, "lua", "object string field")
t.assert_eq(decoded.count, 42, "object integer field")
t.assert_eq(decoded.items[1], 1, "array number field")
t.assert_eq(decoded.items[2], true, "array boolean field")
t.assert_eq(json.type(decoded.items[3]), "null", "array null field")
t.assert_eq(json.type(decoded.emptyArray), "array", "empty array shape")
t.assert_eq(json.type(decoded.emptyObject), "object", "empty object shape")

t.start("encode round trips tables and null values")
local payload = json.object({
    name = "payload",
    list = json.array(1, nil, 3),
    empty_array = json.array(),
    empty_object = json.object(),
    missing = json.null,
})
local encoded = json.stringify(payload)
local roundtrip = json.parse(encoded)
t.assert_eq(roundtrip.name, "payload", "roundtrip object field")
t.assert_eq(roundtrip.list[1], 1, "roundtrip array value")
t.assert_eq(json.type(roundtrip.list[2]), "null", "nil vararg encoded as null")
t.assert_eq(json.type(roundtrip.empty_array), "array", "roundtrip empty array")
t.assert_eq(json.type(roundtrip.empty_object), "object", "roundtrip empty object")
t.assert_eq(json.type(roundtrip.missing), "null", "roundtrip explicit null")
t.assert_eq(json.encode(json.array(1, nil, 3)), "[1,null,3]", "array constructor preserves nil")

t.start("helpers classify JSON-oriented Lua values")
local arr = json.array("x")
local obj = json.object({ x = 1 })
local dense = { 1, 2 }
local keyed = { x = 1 }
t.assert_eq(json.is_array(arr), true, "marked array")
t.assert_eq(json.is_object(obj), true, "marked object")
t.assert_eq(json.type(dense), "array", "dense table inferred as array")
t.assert_eq(json.type(keyed), "object", "keyed table inferred as object")
t.assert_eq(json.is_null(json.null), true, "json.null is null")
t.assert_eq(json.is_null(nil), true, "nil is null for helper checks")

t.start("validation and formatting")
local valid = json.validate('{"a":1}')
local invalid, validation_error = json.validate('{')
t.assert_eq(valid, true, "valid JSON")
t.assert_eq(invalid, false, "invalid JSON")
t.assert_eq(type(validation_error), "string", "validation returns error message")
local minified = json.minify(' { "a" : [ 1, 2 ] } ')
t.assert_eq(minified, '{"a":[1,2]}', "minify")
local prettified = json.prettify(minified)
t.assert_truthy(prettified:find("\n", 1, true) ~= nil, "prettify adds newlines")

t.start("JSONC comments option")
local jsonc = json.decode('{/* comment */"a":1}', { comments = true })
t.assert_eq(jsonc.a, 1, "decode JSONC")
t.assert_eq(json.validate('{/* comment */"a":1}', { comments = true }), true, "validate JSONC")

t.start("safe API returns nil plus error")
local safe_value, safe_error = json_safe.decode('{')
t.assert_eq(safe_value, nil, "safe decode returns nil")
t.assert_eq(type(safe_error), "string", "safe decode returns error string")
t.assert_contains(safe_error, "json decode", "safe decode error context")

t.start("file load and save")
local file_path = "logs/lua_json_api_test.json"
if os and os.remove then
    os.remove(file_path)
end
local save_ok = json.write_file(file_path, payload, { pretty = true })
t.assert_eq(save_ok, true, "write_file returns true")
local loaded = json.read_file(file_path)
t.assert_eq(loaded.name, "payload", "read_file object field")
t.assert_eq(json.type(loaded.empty_array), "array", "read_file empty array")
t.assert_eq(json.type(loaded.missing), "null", "read_file null")
if os and os.remove then
    os.remove(file_path)
end

t.start("encode error paths")
local cycle = {}
cycle.self = cycle
local ok_cycle, err_cycle = pcall(function()
    json.encode(cycle)
end)
t.assert_eq(ok_cycle, false, "cycle is rejected")
t.assert_contains(err_cycle, "cycle", "cycle error message")

local sparse = {}
sparse[1] = "a"
sparse[3] = "c"
json.as_array(sparse)
local ok_sparse, err_sparse = pcall(function()
    json.encode(sparse)
end)
t.assert_eq(ok_sparse, false, "sparse marked array is rejected")
t.assert_contains(err_sparse, "dense positive integer", "sparse array error message")

assert(t.summary())
