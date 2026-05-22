-- engine_init.lua
-- Engine lifecycle script — called automatically at startup.
-- Define these three global functions to hook into the engine lifecycle.

function InitScript()
    -- Called once when the engine starts, after all scripts are loaded.
    -- Use this to initialize game state, register handlers, load config, etc.
end

function UpdateScript()
    -- Called every frame.
    -- Use this for per-frame game logic, input polling, etc.
end

function DestroyScript()
    -- Called once when the engine shuts down.
    -- Use this to persist state, close connections, release resources, etc.
end
