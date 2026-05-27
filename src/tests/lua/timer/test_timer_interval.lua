-- test_timer_interval.lua
-- Tests for periodic (interval) timer via Lua bindings.

local t = import("tests.harness.test_harness")

t.start("interval timer fires multiple times")
local count = 0
local max_count = 3
local tid = timer.interval(30, function()
    count = count + 1
    if count >= max_count then
        timer.cancel(tid)
    end
end)
t.assert_truthy(tid ~= nil, "interval timer created")
t.assert_truthy(type(tid) == "number", "timer id is number")

-- Check after enough time has passed
timer.timeout(200, function()
    t.assert_eq(count, max_count, "interval fired " .. max_count .. " times")
    t.summary()
    if count == max_count then
        os.exit(0)
    else
        os.exit(1)
    end
end)
