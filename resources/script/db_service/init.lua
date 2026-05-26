-- db_service/init.lua
-- Database service script entry point.
-- Loaded by DBThread EventLoop on startup (per-thread DBScriptVM).
-- mongoc / bson modules and db_get_client / db_get_pool are already
-- registered by the time this script runs.

log_info("=== DB Service module loaded ===")

-- Example: pre-load shared DB helpers
-- import("db_service.helpers")

return {}
