--- Timer API
--- 全局模块: timer
--- 所有回调在事件循环线程上调用，无参数。

--- 创建一次性定时器，ms 毫秒后触发 callback，随后自动销毁。
---@param ms       integer   延迟毫秒数 (>= 1)
---@param callback function  到期时回调: fun()
---@return integer timer_id  用于 timer.cancel 的标识
function timer.timeout(ms, callback) end

--- 创建周期性定时器，每隔 ms 毫秒重复触发 callback。
---@param ms       integer   间隔毫秒数 (>= 1)
---@param callback function  到期时回调: fun()
---@return integer timer_id  用于 timer.cancel 的标识
function timer.interval(ms, callback) end

--- 取消并销毁定时器。在 callback 内调用自身 cancel 也是安全的。
---@param timer_id integer  timer.timeout / timer.interval 返回的 id
---@return true      成功取消
---@return nil, "timer not found"  未找到该定时器
function timer.cancel(timer_id) end
