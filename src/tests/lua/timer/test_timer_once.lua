-- test_timer_once.lua
-- Tests for one-shot (timeout) timer via Lua bindings.

local t = import("tests.harness.test_harness")

-- NOP: verify harness works
t.start("harness sanity")
t.assert_eq(1 + 1, 2, "basic math")
t.assert_truthy(true, "truthy")
t.assert_gt(5, 3, "greater than")

-- One-shot timer fires once
t.start("oneshot timer fires")
local fired = false
local tid = timer.timeout(50, function()
    fired = true
end)
t.assert_truthy(tid ~= nil, "timer created")
t.assert_truthy(type(tid) == "number", "timer id is number")

-- Wait for the timer to fire
timer.timeout(100, function()
    t.assert_truthy(fired, "timer callback fired")
    t.summary()
    -- Signal completion to the runner
    if fired then
        os.exit(0)
    else
        os.exit(1)
    end
end)

-- Prevent script from ending before async checks complete
