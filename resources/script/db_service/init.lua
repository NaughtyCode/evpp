-- db_service/init.lua
-- Database service script entry point.
-- Loaded by DBThread EventLoop on startup (per-thread DBScriptVM).
-- mongoc / bson modules and db_get_client / db_get_pool are already
-- registered by the time this script runs.

log_info("=== DB Service module loaded ===")

-- Per-frame callback invoked by DBThread EventLoop after request processing.
-- Called once per EventLoop iteration (frame), before frame-rate sleep.
-- info = { frame_count = <int>, delta_seconds = <number> }
-- If you don't need per-frame logic, simply delete or comment out this function.
function on_db_frame(info)
    -- Example: log a heartbeat every 600 frames (~5s at 120fps)
    -- if info.frame_count % 600 == 0 then
    --     log_info(string.format("DBThread heartbeat frame=%d delta=%.4f",
    --                            info.frame_count, info.delta_seconds))
    -- end
end

-- Example: pre-load shared DB helpers
-- import("db_service.helpers")

return {}
