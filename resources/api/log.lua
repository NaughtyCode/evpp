--- Logging API
--- 所有函数均为全局函数，以 "[lua]" 前缀输出到 C++ quill 异步日志。

--- 输出 TRACE 级别日志。
---@param msg string  消息内容
function log_trace(msg) end

--- 输出 DEBUG 级别日志。
---@param msg string  消息内容
function log_debug(msg) end

--- 输出 INFO 级别日志。
---@param msg string  消息内容
function log_info(msg) end

--- 输出 WARN 级别日志。
---@param msg string  消息内容
function log_warn(msg) end

--- 输出 ERROR 级别日志。
---@param msg string  消息内容
function log_error(msg) end

--- 输出 FATAL 级别日志。
---@param msg string  消息内容
function log_fatal(msg) end
