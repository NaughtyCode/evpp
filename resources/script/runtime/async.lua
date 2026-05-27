--[[
    async.lua — coroutine-based async programming module.

    Provides sleep, await, and primitives for writing non-blocking async
    code using Lua coroutines.

    Usage:
        local async = require("async")

        -- Sleep for 1 second
        async.sleep(1000)

        -- Run a function asynchronously and wait for its result
        local result = async.await(function()
            -- do work, return result
            return 42
        end)
]]

local async = {}

--- Suspend the current coroutine for `ms` milliseconds.
--- @param ms number  milliseconds to sleep
function async.sleep(ms)
    local co = coroutine.running()
    if not co then
        -- Not in a coroutine — just block (fallback)
        return
    end
    local ms_int = math.floor(tonumber(ms) or 0)
    if ms_int <= 0 then return end

    -- Create a one-shot timer that resumes this coroutine
    timer = timer or rawget(_G, "timer")
    if timer and timer.timeout then
        timer.timeout(ms_int, function()
            local ok, err = coroutine.resume(co)
            if not ok then
                local log = rawget(_G, "log_err") or rawget(_G, "print")
                log("[async] sleep resume error: " .. tostring(err))
            end
        end)
        coroutine.yield()
    else
        -- No timer available — just yield and hope to be resumed on next frame
        coroutine.yield()
    end
end

--- Execute a function in a coroutine and return its result.
--- The function is run immediately and the caller is suspended until it completes.
--- @param fn function  the function to execute
--- @return any  the return value of fn
function async.await(fn)
    local co = coroutine.create(fn)
    local results = {}
    local done = false

    local function step()
        if done then return end
        local ok, val = coroutine.resume(co)
        if coroutine.status(co) == "dead" then
            done = true
            if ok then
                table.insert(results, val)
            else
                error(val)
            end
        else
            -- Coroutine yielded — wait for next resume
        end
    end

    step()
    -- If the coroutine yielded, the caller's coroutine is suspended
    -- and will be resumed when the async operation completes.
    if not done then
        coroutine.yield()
        step()
    end

    if #results > 0 then
        return results[1]
    end
    return nil
end

--- Run a function in a coroutine (fire-and-forget).
--- @param fn function
function async.run(fn)
    local co = coroutine.create(fn)
    local function step()
        if coroutine.status(co) == "dead" then return end
        local ok, err = coroutine.resume(co)
        if not ok then
            local log = rawget(_G, "log_err") or rawget(_G, "print")
            log("[async] run error: " .. tostring(err))
        elseif coroutine.status(co) ~= "dead" then
            -- Coroutine yielded — schedule resume next frame
            local timer = rawget(_G, "timer")
            if timer and timer.timeout then
                timer.timeout(0, step)
            end
        end
    end
    step()
end

return async
