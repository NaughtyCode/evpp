-- engine_init.lua
-- Engine lifecycle script — called automatically at startup.
-- Define these three global functions to hook into the engine lifecycle.

function InitScript()
    -- Called once when the engine starts, after all scripts are loaded.
    log_info("=== InitScript ===")

    -- Example: create a repeating timer that fires every 2 seconds
    -- local tid = timer.interval(2000, function()
    --     log_info("tick from Lua timer")
    -- end)

    -- Example: create a one-shot timeout that fires after 5 seconds
    -- timer.timeout(5000, function()
    --     log_info("one-shot timeout fired!")
    -- end)

    -- Example: cancel a timer
    -- timer.cancel(tid)
end

function UpdateScript()
    -- Called every frame.
end

function DestroyScript()
    -- Called once when the engine shuts down.
    log_info("=== DestroyScript ===")
end
