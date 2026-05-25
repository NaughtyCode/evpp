--- TCP Server API
--- Module: net.server
---
--- 基于 libevent2 + evpp 的异步 TCP 服务器。
--- 通过 net.server.listen(addr) 创建并启动服务器，
--- 返回一个服务器实例表。新连接到达时通过 on_connect 回调
--- 传入连接实例表，支持服务器级别和连接级别的回调覆盖。
---
--- 所有回调均在主线程的事件循环中异步触发。
--- 连接级别的 on_message / on_close 回调优先级高于服务器级别的默认回调。

-- ============================================================================
-- net.server 静态方法
-- ============================================================================

--- 创建并启动 TCP 服务器，监听指定地址。
---
--- 创建一个 TCP 服务器实例，绑定到 host:port 格式的地址并开始监听。
--- 返回服务器实例表。Init 或 Start 失败时抛出 Lua error。
---
--- 使用示例:
--- ```lua
--- local server = net.server.listen("0.0.0.0:8080")
--- server:set_on_connect(function(conn, remote_addr)
---     print("new connection from", remote_addr)
---     conn:set_on_message(function(data)
---         print("recv:", data)
---         conn:send("echo: " .. data)
---     end)
---     conn:set_on_close(function(addr)
---         print("disconnected:", addr)
---     end)
--- end)
--- ```
---
---@param addr string  监听地址，格式 "host:port"，例如 "0.0.0.0:8080"
---@return table server_instance  服务器实例表，失败时抛出 Lua error
function net.server.listen(addr) end

-- ============================================================================
-- 服务器实例方法
-- ============================================================================

--- 停止服务器并释放所有资源。
---
--- 停止服务器，断开所有连接，释放所有回调引用。
--- C++ 对象会在事件循环的下一个周期中被析构。
--- 调用后该实例不再可用。
---
---@return boolean true  成功停止；false 已停止（无效或重复调用）
function server:stop() end

--- 设置新连接回调（服务器级别）。
---
--- 当有新客户端连接时触发。回调接收连接实例表和远程地址。
--- 通常在回调中为每个连接设置 on_message 和 on_close 回调。
---
---@param callback function  回调函数: fun(conn: table, remote_addr: string)
function server:set_on_connect(callback) end

--- 设置消息接收回调（服务器级别默认）。
---
--- 当连接收到数据且该连接未设置自己的 on_message 时，
--- 使用此服务器级别的默认回调处理。
---
---@param callback function  回调函数: fun(conn: table, data: string)
function server:set_on_message(callback) end

--- 设置连接关闭回调（服务器级别默认）。
---
--- 当连接断开且该连接未设置自己的 on_close 时，
--- 使用此服务器级别的默认回调处理。
---
---@param callback function  回调函数: fun(conn: table, remote_addr: string)
function server:set_on_close(callback) end

-- ============================================================================
-- 连接实例方法（由 on_connect 回调传入）
-- ============================================================================

--- 向此连接发送数据。
---
--- 发送原始字节数据到该 TCP 连接的对端。连接断开时调用会抛出 error。
---
---@param data string  要发送的原始数据
function conn:send(data) end

--- 关闭此连接并释放资源。
---
--- 主动关闭连接，释放 Lua 回调引用。注意：手动 close() 不会触发 on_close 回调。
--- C++ 对象会在事件循环的下一个周期中被析构。
---
---@return boolean true  成功关闭；false 已关闭（无效或重复调用）
function conn:close() end

--- 检查连接是否处于活动状态。
---
---@return boolean  true 已连接，false 未连接或已关闭
function conn:is_connected() end

--- 设置此连接的消息接收回调（连接级别）。
---
--- 设置后将覆盖服务器级别的 on_message 默认回调。
--- 当此连接收到数据时调用。
---
---@param callback function  回调函数: fun(data: string)
function conn:set_on_message(callback) end

--- 设置此连接的关闭回调（连接级别）。
---
--- 设置后将覆盖服务器级别的 on_close 默认回调。
--- 当此连接断开时调用（不包括手动 close()）。
---
---@param callback function  回调函数: fun(remote_addr: string)
function conn:set_on_close(callback) end
