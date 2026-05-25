--- UDP Server API
--- Module: net.udp_server
---
--- 基于多线程 UDP socket 的 UDP 服务器。
--- 通过 net.udp_server.listen(port) 创建并启动服务器。
--- 消息在独立的 recv 线程中接收，然后通过 RunInLoop 调度到
--- 事件循环主线程中执行 Lua 回调，确保线程安全。
---
--- 支持单端口或多端口监听（用逗号分隔的端口字符串）。
--- 支持暂停/继续接收，以及运行时替换消息回调。

-- ============================================================================
-- net.udp_server 静态方法
-- ============================================================================

--- 创建并启动 UDP 服务器，监听指定端口。
---
--- 创建一个 UDP 服务器实例，绑定到指定端口并开始接收数据。
--- 返回服务器实例表。失败时返回 nil 和错误信息。
---
--- 使用示例:
--- ```lua
--- local server = net.udp_server.listen(5353, function(data, remote_ip)
---     print("recv from", remote_ip, ":", data)
--- end)
--- if not server then
---     print("listen failed")
---     return
--- end
--- -- 或监听多个端口
--- local server = net.udp_server.listen("53,5353", on_message)
--- ```
---
---@param port_or_ports integer|string  监听端口。整数=单端口；字符串="5353" 或 "53,5353"（逗号分隔多端口）
---@param on_message?   function        消息接收回调: fun(data: string, remote_ip: string)
---@return table server_instance  成功: 服务器实例表
---@return nil, string errmsg    失败: 错误信息
function net.udp_server.listen(port_or_ports, on_message) end

-- ============================================================================
-- 服务器实例方法
-- ============================================================================

--- 停止 UDP 服务器并释放所有资源。
---
--- 等待 recv 线程退出后再释放 Lua 引用和 C++ 对象，
--- 确保不会有残留的消息回调在 stop 返回后触发。
---
---@return boolean true  成功停止；false 已停止
function server:stop() end

--- 暂停接收 UDP 消息。
---
--- 暂停后 recv 线程停止读取数据，但服务器仍保持运行状态。
--- 已进入事件循环队列的消息仍会正常触发回调。
function server:pause() end

--- 继续接收 UDP 消息（恢复暂停的服务器）。
---
--- 恢复 recv 线程的数据读取。
function server:continue() end

--- 检查服务器是否处于运行状态。
---
---@return boolean  true 运行中，false 已停止
function server:is_running() end

--- 设置/替换消息接收回调。
---
--- 原子性地替换当前的消息回调。旧的回调引用会在事件循环中
--- 安全释放（等待已投递的消息任务完成后再 unref）。
---
---@param callback function  消息接收回调: fun(data: string, remote_ip: string)
function server:set_on_message(callback) end
