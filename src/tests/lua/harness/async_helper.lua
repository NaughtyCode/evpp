-- async_helper.lua
-- Helpers for async callback-driven tests.
-- Uses engine timers for coordination.

local M = {}

-- Run a function and wait for a condition to become true.
-- Calls `done()` once the condition is met.
-- Fails the test if `timeout_ms` elapses first.
function M.wait_for(cond_fn, timeout_ms, done)
    timeout_ms = timeout_ms or 3000
    local start = os.clock() * 1000

    local function check()
        if cond_fn() then
            done()
            return
        end
        if (os.clock() * 1000) - start > timeout_ms then
            log_error("  [FAIL] async wait timed out after " .. timeout_ms .. "ms")
            done()
            return
        end
        timer.timeout(20, check)
    end
    timer.timeout(10, check)
end

-- Chain a series of async test functions.
-- Each receives `next_fn` as its argument and must call it when done.
function M.run_tests(tests, done)
    local i = 1
    local function next_fn()
        if tests[i] then
            local test = tests[i]
            i = i + 1
            test(next_fn)
        else
            if done then done() end
        end
    end
    next_fn()
end

return M
