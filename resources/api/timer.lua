--- Timer API
--- Global module: timer
--- All callbacks are invoked on the event loop thread with no arguments.

--- Create a one-shot timer that fires the callback after ms milliseconds, then auto-destroys.
---@param ms       integer   delay in milliseconds (>= 1)
---@param callback function  callback on expiry: fun()
---@return integer timer_id  identifier for timer.cancel
function timer.timeout(ms, callback) end

--- Create a periodic timer that fires the callback every ms milliseconds.
---@param ms       integer   interval in milliseconds (>= 1)
---@param callback function  callback on expiry: fun()
---@return integer timer_id  identifier for timer.cancel
function timer.interval(ms, callback) end

--- Cancel and destroy a timer. It is safe to call cancel on your own timer inside its callback.
---@param timer_id integer  identifier returned by timer.timeout / timer.interval
---@return true      successfully cancelled
---@return nil, "timer not found"  timer not found
function timer.cancel(timer_id) end
