-- test_harness.lua
-- Shared lightweight test framework for Lua script tests.
--
-- Usage:
--   local t = import("tests.harness.test_harness")
--   t.start("my test")
--   t.assert_eq(1 + 1, 2, "basic math")
--   t.assert_truthy(true, "truthy check")
--   t.summary()

local M = {}

local passed = 0
local failed = 0
local current_test = ""

function M.reset()
    passed = 0
    failed = 0
    current_test = ""
end

function M.start(name)
    current_test = name
    log_info("")
    log_info("=== " .. name .. " ===")
end

function M.assert_truthy(cond, msg)
    if cond then
        passed = passed + 1
        log_info("  [PASS] " .. (msg or "ok"))
    else
        failed = failed + 1
        log_error("  [FAIL] " .. current_test .. ": " .. (msg or "assertion failed"))
    end
end

function M.assert_eq(a, b, msg)
    -- NaN special case
    if type(a) == "number" and type(b) == "number" then
        if a ~= a and b ~= b then
            M.assert_truthy(true, msg or "both NaN")
            return
        end
    end
    local equal = (a == b)
    if not equal then
        local detail = " — expected [" .. tostring(b) .. "], got [" .. tostring(a) .. "]"
        M.assert_truthy(false, (msg or "eq") .. detail)
    else
        M.assert_truthy(true, msg)
    end
end

function M.assert_gt(a, b, msg)
    M.assert_truthy(a > b, msg or ("expected " .. tostring(a) .. " > " .. tostring(b)))
end

function M.assert_contains(haystack, needle, msg)
    local found = string.find(haystack, needle, 1, true) ~= nil
    M.assert_truthy(found, msg or ("expected '" .. needle .. "' in '" .. haystack .. "'"))
end

function M.summary()
    local total = passed + failed
    log_info("")
    log_info("=== " .. total .. " assertion(s): " .. passed .. " passed, " .. failed .. " failed ===")
    if failed > 0 then
        log_error("Some tests FAILED!")
    else
        log_info("All tests PASSED.")
    end
    M.reset()
    return failed == 0
end

return M
