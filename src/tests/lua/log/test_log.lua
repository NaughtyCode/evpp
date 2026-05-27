-- test_log.lua
-- Verifies log functions are available and don't error.

local t = import("tests.harness.test_harness")

t.start("log functions exist")
t.assert_truthy(type(log_info) == "function", "log_info is function")
t.assert_truthy(type(log_error) == "function", "log_error is function")
t.assert_truthy(type(log_debug) == "function", "log_debug is function")

t.start("log functions call without error")
local ok1, err1 = pcall(log_info, "test info message")
t.assert_truthy(ok1, "log_info succeeds — " .. tostring(err1 or ""))

local ok2, err2 = pcall(log_error, "test error message")
t.assert_truthy(ok2, "log_error succeeds — " .. tostring(err2 or ""))

t.summary()
