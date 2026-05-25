-- runtime/init.lua
-- Runtime shared module entry point.
-- Imported by both client and server entry scripts via import("runtime.init").
-- All shared initialization and utility loading happens here.
-- Do NOT define InitScript/UpdateScript/DestroyScript here —
-- those belong in the client or server entry scripts.

log_info("=== Runtime module loaded ===")

-- Load class system (available to all runtime scripts)
import("runtime.common.class")

-- Load additional runtime sub-modules as needed:
-- import("runtime.helpers")
-- import("runtime.common_config")

return {
    -- Expose runtime-level API if needed
}
