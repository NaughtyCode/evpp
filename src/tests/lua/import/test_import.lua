-- test_import.lua
-- Tests for the import() system.

local t = import("tests.harness.test_harness")

t.start("import returns module table")
local harness = import("tests.harness.test_harness")
t.assert_truthy(type(harness) == "table", "import returns table")
t.assert_truthy(type(harness.start) == "function", "harness has start()")
t.assert_truthy(type(harness.assert_eq) == "function", "harness has assert_eq()")

t.start("import caches modules (same table returned)")
local harness2 = import("tests.harness.test_harness")
t.assert_truthy(harness == harness2, "same module instance")

t.summary()
