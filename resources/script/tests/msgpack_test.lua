-- msgpack_test.lua
-- Comprehensive test suite for cmsgpack / cmsgpack_safe bindings.
--
-- Run from Lua:  dofile("resources/script/tests/msgpack_test.lua")
-- Or via the engine lifecycle.

local mp    = cmsgpack
local mps   = cmsgpack_safe

-- ── Test framework ──────────────────────────────────────────────────────────

local passed  = 0
local failed  = 0
local test_name = ""

local function start(name)
    test_name = name
end

local function ok(cond, msg)
    if cond then
        passed = passed + 1
    else
        failed = failed + 1
        log_error(string.format("FAIL [%s]: %s", test_name, msg or "assertion failed"))
    end
end

local function eq(a, b, msg)
    -- Deep compare for tables; shallow otherwise.
    if type(a) == "table" and type(b) == "table" then
        -- Compare keys and values recursively.
        for k, v in pairs(a) do
            if not eq(v, b[k], msg and msg .. "." .. tostring(k)) then
                ok(false, msg and msg .. ": table mismatch")
                return false
            end
        end
        for k, _ in pairs(b) do
            if a[k] == nil then
                ok(false, msg and msg .. ": extra key " .. tostring(k))
                return false
            end
        end
        ok(true, msg)
        return true
    end
    -- NaN special case: NaN ~= NaN
    if type(a) == "number" and type(b) == "number" then
        local a_nan = (a ~= a)
        local b_nan = (b ~= b)
        if a_nan and b_nan then
            ok(true, msg)
            return true
        end
    end
    ok(a == b, msg or ("expected " .. tostring(b) .. ", got " .. tostring(a)))
    return a == b
end

local function neq(a, b, msg)
    ok(a ~= b, msg or ("expected not equal to " .. tostring(b)))
end

local function summary()
    log_info(string.format("=== msgpack test summary: %d passed, %d failed ===", passed, failed))
    if failed > 0 then
        log_error("Some tests FAILED!")
    else
        log_info("All tests passed.")
    end
    return failed == 0
end

-- ════════════════════════════════════════════════════════════════════════════
-- Roundtrip helper
-- ════════════════════════════════════════════════════════════════════════════

local function roundtrip(...)
    local args = table.pack(...)
    local data = mp.pack(...)
    local results = table.pack(mp.unpack(data))
    -- Compare original args with decoded results.
    for i = 1, math.max(args.n, results.n) do
        eq(args[i], results[i], "roundtrip [" .. i .. "]")
    end
end

-- ════════════════════════════════════════════════════════════════════════════
-- 1. Nil encoding
-- ════════════════════════════════════════════════════════════════════════════

start("nil")
local d1 = mp.pack(nil)
local v1 = mp.unpack(d1)
eq(v1, nil)
eq(type(d1), "string")
eq(#d1, 1)   -- nil = 0xc0, 1 byte

-- ════════════════════════════════════════════════════════════════════════════
-- 2. Boolean encoding
-- ════════════════════════════════════════════════════════════════════════════

start("boolean")
eq(mp.unpack(mp.pack(true)),  true)
eq(mp.unpack(mp.pack(false)), false)

-- ════════════════════════════════════════════════════════════════════════════
-- 3. Integer encoding — boundary values for each format
-- ════════════════════════════════════════════════════════════════════════════

start("integer positive fixnum")
roundtrip(0)
roundtrip(1)
roundtrip(127)

start("integer uint8")
roundtrip(128)
roundtrip(255)

start("integer uint16")
roundtrip(256)
roundtrip(65535)

start("integer uint32")
roundtrip(65536)
roundtrip(4294967295)

start("integer uint64")
roundtrip(4294967296)
roundtrip(9223372036854775807)  -- INT64_MAX

start("integer negative fixnum")
roundtrip(-1)
roundtrip(-32)

start("integer int8")
roundtrip(-33)
roundtrip(-128)

start("integer int16")
roundtrip(-129)
roundtrip(-32768)

start("integer int32")
roundtrip(-32769)
roundtrip(-2147483648)

start("integer int64")
roundtrip(-2147483649)
roundtrip(-9223372036854775807)
roundtrip(-9223372036854775808)  -- INT64_MIN

-- ════════════════════════════════════════════════════════════════════════════
-- 4. Float encoding
-- ════════════════════════════════════════════════════════════════════════════

start("float basic")
roundtrip(1.5)
roundtrip(-3.14)

start("float integrable as int64")
-- Floats with no fractional part that fit in int64 → encoded as int.
roundtrip(42.0)   -- should encode as positive fixnum, decode as 42 (int)
local v = mp.unpack(mp.pack(42.0))
eq(type(v), "number")
eq(v, 42)         -- value same; type may change (float→int) per original behavior

start("float double")
roundtrip(1e100)
roundtrip(-1e100)

start("float special")
-- inf / -inf
local dinf = mp.pack(math.huge)
local vinf = mp.unpack(dinf)
eq(vinf, math.huge)

local dninf = mp.pack(-math.huge)
local vninf = mp.unpack(dninf)
eq(vninf, -math.huge)

-- -0.0
local dmzero = mp.pack(-0.0)
local vmzero = mp.unpack(dmzero)
-- -0.0 == 0.0 is true in Lua, but we verify it roundtrips.
eq(vmzero, 0)

-- ════════════════════════════════════════════════════════════════════════════
-- 5. String encoding
-- ════════════════════════════════════════════════════════════════════════════

start("string empty")
roundtrip("")

start("string fixstr (len < 32)")
roundtrip("hello")
roundtrip(string.rep("x", 31))

start("string str8")
roundtrip(string.rep("x", 32))
roundtrip(string.rep("x", 255))

start("string str16")
roundtrip(string.rep("y", 256))
roundtrip(string.rep("y", 65535))

start("string binary")
-- Strings with embedded nulls and non-printable bytes.
local bin = string.char(0, 1, 2, 128, 254, 255, 0, 0)
roundtrip(bin)

start("string unicode")
roundtrip("héllo wörld — utf-8 ✓")
roundtrip("Chinese test")

-- ════════════════════════════════════════════════════════════════════════════
-- 6. Table (array) encoding
-- ════════════════════════════════════════════════════════════════════════════

start("table empty array")
roundtrip({})

start("table fixarray")
roundtrip({1, 2, 3})
roundtrip({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15})

start("table array16")
local a16 = {}
for i = 1, 16 do a16[i] = i end
roundtrip(a16)

start("table nested array")
roundtrip({1, {2, 3}, {4, {5, 6}}})

-- ════════════════════════════════════════════════════════════════════════════
-- 7. Table (map) encoding
-- ════════════════════════════════════════════════════════════════════════════

start("table fixmap")
roundtrip({a = 1})
roundtrip({a = 1, b = 2, c = 3})

start("table map16")
local m16 = {}
for i = 1, 16 do m16["k" .. i] = i end
roundtrip(m16)

start("table nested map")
roundtrip({a = {b = {c = 1}}})

start("table mix of array-like and map")
-- Table with both numeric keys and string keys → map
local mixed = {10, 20, 30, name = "test"}
local dmixed = mp.pack(mixed)
local vmixed = mp.unpack(dmixed)
eq(type(vmixed), "table")
eq(vmixed[1], 10)
eq(vmixed[2], 20)
eq(vmixed[3], 30)
eq(vmixed.name, "test")

-- ════════════════════════════════════════════════════════════════════════════
-- 8. Table with holes → map (not array)
-- ════════════════════════════════════════════════════════════════════════════

start("table with holes (should be map)")
local holey = {[1] = 1, [3] = 3}
local dholey = mp.pack(holey)
local vholey = mp.unpack(dholey)
-- Both keys should be present.
eq(vholey[1], 1)
eq(vholey[3], 3)

-- ════════════════════════════════════════════════════════════════════════════
-- 9. Deeply nested table — should not overflow
-- ════════════════════════════════════════════════════════════════════════════

start("deeply nested table (15 levels, just below max)")
local deep = 0
for i = 1, 15 do deep = {deep} end
local ok_deep, data_deep = pcall(mp.pack, deep)
ok(ok_deep, "deep table pack should succeed")
if ok_deep then
    local vdeep = mp.unpack(data_deep)
    -- Verify it roundtrips.
    local d = vdeep
    local depth = 1
    while type(d) == "table" and d[1] ~= nil do
        d = d[1]
        depth = depth + 1
    end
    eq(depth, 16)
end

-- ════════════════════════════════════════════════════════════════════════════
-- 10. Table with key = 0 → treated as map
-- ════════════════════════════════════════════════════════════════════════════

start("table with zero key → map")
local zt = {[0] = "zero", hello = "world"}
local dzt = mp.pack(zt)
local vzt = mp.unpack(dzt)
eq(vzt[0], "zero")
eq(vzt.hello, "world")

-- ════════════════════════════════════════════════════════════════════════════
-- 11. Multiple arguments
-- ════════════════════════════════════════════════════════════════════════════

start("pack multiple args")
local dm = mp.pack(1, "two", true, nil, {3, 4})
local v1, v2, v3, v4, v5 = mp.unpack(dm)
eq(v1, 1)
eq(v2, "two")
eq(v3, true)
eq(v4, nil)
eq(type(v5), "table")
eq(v5[1], 3)
eq(v5[2], 4)

-- ════════════════════════════════════════════════════════════════════════════
-- 12. Stream of multiple top-level objects
-- ════════════════════════════════════════════════════════════════════════════

start("multiple top-level objects in one buffer")
local stream = mp.pack(1) .. mp.pack(2) .. mp.pack(3)
local r1, r2, r3 = mp.unpack(stream)
eq(r1, 1)
eq(r2, 2)
eq(r3, 3)

-- ════════════════════════════════════════════════════════════════════════════
-- 13. unpack_one
-- ════════════════════════════════════════════════════════════════════════════

start("unpack_one basic")
local dstream = mp.pack(100, 200, 300)
local v, off = mp.unpack_one(dstream)
eq(v, 100)
eq(type(off), "number")
neq(off, -1)  -- should have more data

start("unpack_one chain")
local next_off = 0
local vv, noff = mp.unpack_one(dstream, next_off)
eq(vv, 100)
vv, noff = mp.unpack_one(dstream, noff)
eq(vv, 200)
vv, noff = mp.unpack_one(dstream, noff)
eq(vv, 300)
eq(noff, -1)  -- done

start("unpack_one single element → offset = -1")
local single = mp.pack(42)
local vv, off = mp.unpack_one(single)
eq(vv, 42)
eq(off, -1)

-- ════════════════════════════════════════════════════════════════════════════
-- 14. unpack_limit
-- ════════════════════════════════════════════════════════════════════════════

start("unpack_limit basic")
local many = mp.pack(1, 2, 3, 4, 5)
local a, b, c, off = mp.unpack_limit(many, 3)
eq(a, 1)
eq(b, 2)
eq(c, 3)
neq(off, -1)  -- more data remains

start("unpack_limit with explicit offset = 0")
local a2, b2, c2, off2 = mp.unpack_limit(many, 3, 0)
eq(a2, 1)
eq(b2, 2)
eq(c2, 3)

-- ════════════════════════════════════════════════════════════════════════════
-- 15. cmsgpack_safe — success path
-- ════════════════════════════════════════════════════════════════════════════

start("cmsgpack_safe pack success")
local ok_s, data_s = mps.pack(1, "hello")
eq(ok_s, nil)     -- no error wrapper on success in safe mode? Actually:
-- safe returns ALL results from the wrapped function on success.
-- pack returns 1 result (the packed string).
-- So safe returns that 1 result directly (no nil prefix).
eq(type(data_s), "string")

start("cmsgpack_safe unpack success")
local ok_s, v_s = mps.unpack(mp.pack(42))
-- unpack returns 1 value (42). Safe returns it directly.
eq(v_s, 42)

-- ════════════════════════════════════════════════════════════════════════════
-- 16. cmsgpack_safe — error path
-- ════════════════════════════════════════════════════════════════════════════

start("cmsgpack_safe pack with no args → error")
local err1, msg1 = mps.pack()
eq(err1, nil)
eq(type(msg1), "string")
ok(string.find(msg1, "pack needs input") ~= nil,
   "error message should mention 'pack needs input'")

start("cmsgpack_safe unpack bad format → error")
local err2, msg2 = mps.unpack("\xc1")  -- 0xc1 is never used
eq(err2, nil)
eq(type(msg2), "string")
ok(string.find(msg2, "Bad data format") ~= nil,
   "error message should mention 'Bad data format'")

start("cmsgpack_safe unpack truncated → error")
local err3, msg3 = mps.unpack("\xcc")  -- uint8 header with no data byte
eq(err3, nil)
eq(type(msg3), "string")
ok(string.find(msg3, "Missing bytes") ~= nil,
   "error message should mention 'Missing bytes'")

-- ════════════════════════════════════════════════════════════════════════════
-- 17. Error paths (unsafe module)
-- ════════════════════════════════════════════════════════════════════════════

start("unsafe pack no args → error")
local ok_err, msg_err = pcall(mp.pack)
ok(not ok_err, "should raise error")
eq(type(msg_err), "string")

start("unsafe unpack bad data → error")
local ok_err2, msg_err2 = pcall(mp.unpack, "\xc1")
ok(not ok_err2, "should raise error")
eq(type(msg_err2), "string")

start("unsafe unpack truncated → error")
local ok_err3, msg_err3 = pcall(mp.unpack, "\xcc")
ok(not ok_err3, "should raise error")

start("unpack_one invalid offset type")
local ok_err4, msg_err4 = pcall(mp.unpack_one, mp.pack(1), "not_a_number")
ok(not ok_err4, "should raise error for non-numeric offset")

start("unpack negative offset")
local ok_err5, msg_err5 = pcall(mp.unpack_one, mp.pack(1), -5)
ok(not ok_err5, "should raise error for negative offset")

start("unpack offset > length")
local ok_err6, msg_err6 = pcall(mp.unpack_one, mp.pack(1), 9999)
ok(not ok_err6, "should raise error for offset beyond length")

start("unpack_limit negative limit")
local ok_err7, msg_err7 = pcall(mp.unpack_limit, mp.pack(1), -1)
ok(not ok_err7, "should raise error for negative limit")

-- ════════════════════════════════════════════════════════════════════════════
-- 18. Module metadata
-- ════════════════════════════════════════════════════════════════════════════

start("cmsgpack metadata")
eq(cmsgpack._NAME, "cmsgpack")
eq(cmsgpack._VERSION, "lua-cmsgpack 0.4.0")
ok(type(cmsgpack._COPYRIGHT) == "string")
ok(type(cmsgpack._DESCRIPTION) == "string")

start("cmsgpack_safe metadata")
eq(cmsgpack_safe._NAME, "cmsgpack")
eq(cmsgpack_safe._VERSION, "lua-cmsgpack 0.4.0")

-- ════════════════════════════════════════════════════════════════════════════
-- 19. Roundtrip: complex mixed data
-- ════════════════════════════════════════════════════════════════════════════

start("complex mixed roundtrip")
local complex = {
    id = 12345,
    name = "test entity",
    active = true,
    score = 99.5,
    tags = {"tag1", "tag2", "tag3"},
    metadata = {
        created = 1700000000,
        flags = {a = 1, b = 0, c = 1},
    },
    children = {
        {id = 1, val = "one"},
        {id = 2, val = "two"},
    },
}
local dcomp = mp.pack(complex)
local vcomp = mp.unpack(dcomp)

eq(vcomp.id, 12345)
eq(vcomp.name, "test entity")
eq(vcomp.active, true)
eq(vcomp.score, 99.5)
eq(vcomp.tags[1], "tag1")
eq(vcomp.tags[3], "tag3")
eq(vcomp.metadata.created, 1700000000)
eq(vcomp.metadata.flags.a, 1)
eq(vcomp.children[1].id, 1)
eq(vcomp.children[2].val, "two")

-- ════════════════════════════════════════════════════════════════════════════
-- 20. Known-good MessagePack bytes (cross-compatibility)
-- ════════════════════════════════════════════════════════════════════════════

start("decode known-good: positive fixnum 42")
eq(mp.unpack("\x2a"), 42)

start("decode known-good: negative fixnum -1")
eq(mp.unpack("\xff"), -1)

start("decode known-good: nil")
eq(mp.unpack("\xc0"), nil)

start("decode known-good: true")
eq(mp.unpack("\xc3"), true)

start("decode known-good: false")
eq(mp.unpack("\xc2"), false)

start("decode known-good: fixstr 'hi'")
eq(mp.unpack("\xa2hi"), "hi")

start("decode known-good: fixarray [1,2,3]")
local arr = mp.unpack("\x93\x01\x02\x03")
eq(#arr, 3)
eq(arr[1], 1)
eq(arr[2], 2)
eq(arr[3], 3)

start("decode known-good: fixmap {a=1}")
local mm = mp.unpack("\x81\xa1" .. "a" .. "\x01")
eq(type(mm), "table")
eq(mm.a, 1)

start("decode known-good: float 32 (1.5)")
-- 1.5 as IEEE 754 float32 big-endian: 0x3fc00000
local f32 = mp.unpack("\xca\x3f\xc0\x00\x00")
eq(math.abs(f32 - 1.5) < 0.0001, true, "float32 approx 1.5")

start("decode known-good: double 64 (1.5)")
-- 1.5 as IEEE 754 float64 big-endian: 0x3ff8000000000000
local f64 = mp.unpack("\xcb\x3f\xf8\x00\x00\x00\x00\x00\x00")
eq(f64, 1.5)

start("decode known-good: uint8 200")
eq(mp.unpack("\xcc\xc8"), 200)

start("decode known-good: int8 -100")
eq(mp.unpack("\xd0\x9c"), -100)

start("decode known-good: uint16 1000")
eq(mp.unpack("\xcd\x03\xe8"), 1000)

start("decode known-good: int16 -1000")
eq(mp.unpack("\xd1\xfc\x18"), -1000)

start("decode known-good: uint32 100000")
eq(mp.unpack("\xce\x00\x01\x86\xa0"), 100000)

start("decode known-good: int32 -100000")
eq(mp.unpack("\xd2\xff\xfe\x79\x60"), -100000)

start("decode known-good: str8 'hello'")
eq(mp.unpack("\xd9\x05hello"), "hello")

start("decode known-good: array16 with 20 zeros")
local zeros = string.rep("\x00", 20)
local darr16 = "\xdc\x00\x14" .. zeros
local varr16 = mp.unpack(darr16)
eq(type(varr16), "table")
eq(#varr16, 20)
eq(varr16[1], 0)
eq(varr16[20], 0)

-- ════════════════════════════════════════════════════════════════════════════
-- 21. Table identity — non-string keys
-- ════════════════════════════════════════════════════════════════════════════

start("table with integer keys > 0 (array)")
local tk = {[1] = "a", [2] = "b", [3] = "c"}
local dtk = mp.pack(tk)
local vtk = mp.unpack(dtk)
eq(vtk[1], "a")
eq(vtk[2], "b")
eq(vtk[3], "c")

start("table with mixed numeric keys (not a dense array)")
local tkn = {[1] = "a", [5] = "e"}
local dtkn = mp.pack(tkn)
local vtkn = mp.unpack(dtkn)
eq(vtkn[1], "a")
eq(vtkn[5], "e")

-- ════════════════════════════════════════════════════════════════════════════
-- 22. Circular table — should not cause stack overflow (max nesting = 16)
-- ════════════════════════════════════════════════════════════════════════════

start("circular table (max nesting = 16, should encode as nil beyond)")
local a = {}
local b = {a}
a[1] = b   -- a = {b}, b = {a}, circular
local okc, dc = pcall(mp.pack, a)
ok(okc, "circular table should not crash")
if okc then
    -- It should produce valid MessagePack (circular ref becomes nil at max depth).
    local vc = mp.unpack(dc)
    ok(type(vc) == "table", "should decode as table")
end

-- ════════════════════════════════════════════════════════════════════════════
-- Final summary
-- ════════════════════════════════════════════════════════════════════════════

log_info("")
return summary()
