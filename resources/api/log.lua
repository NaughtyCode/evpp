--- Logging API
--- All functions are global and output to the C++ quill async log with a "[lua]" prefix.

--- Output a TRACE level log message.
---@param msg string  message content
function log_trace(msg) end

--- Output a DEBUG level log message.
---@param msg string  message content
function log_debug(msg) end

--- Output an INFO level log message.
---@param msg string  message content
function log_info(msg) end

--- Output a WARN level log message.
---@param msg string  message content
function log_warn(msg) end

--- Output an ERROR level log message.
---@param msg string  message content
function log_error(msg) end

--- Output a FATAL level log message.
---@param msg string  message content
function log_fatal(msg) end
