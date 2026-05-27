-- smoke_lua.lua
-- Minimal smoke test: loads the test harness and runs a single assertion.

local t = import("tests.harness.test_harness")
t.start("smoke: harness loads")
t.assert_truthy(true, "truth passes")
t.summary()
