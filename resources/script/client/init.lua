-- client/init.lua
-- Client entry script — loaded automatically at startup.
-- First loads all shared runtime modules, then defines client lifecycle hooks.

-- Load all shared runtime modules
import("runtime.init")

function InitScript()
    -- Called once when the engine starts, after all scripts are loaded.
    log_info("=== Client InitScript ===")

    -- Client-specific initialization goes here
end

function UpdateScript()
    -- Called every frame.
    -- Client-specific per-frame logic goes here
end

function DestroyScript()
    -- Called once when the engine shuts down.
    log_info("=== Client DestroyScript ===")
end
