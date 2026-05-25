--- TCP Client API
--- Module: net.client
---
--- 基于 libevent2 + evpp 的异步 TCP 客户端。
--- 通过 net.client.connect(addr) 创建并连接到远程主机，
--- 返回一个客户端实例表，后续操作均通过该实例的方法进行。
---
--- 所有回调均在主线程的事件循环中异步触发。
--- 回调通过 set_on_* 方法设置，存储在实例表字段中。

-- ============================================================================
-- net.client 静态方法
-- ============================================================================

--- 创建 TCP 客户端并连接到指定地址。
---
--- 创建一个 TCP 客户端实例，连接到 host:port 格式的地址，
--- 返回客户端实例表。连接结果通过 on_connect / on_close 回调通知。
---
--- 使用示例:
--- ```lua
--- local client = net.client.connect("127.0.0.1:8080")
--- client:set_on_connect(function()
---     print("connected!")
---     client:send("hello")
--- end)
--- client:set_on_message(function(data)
---     print("recv:", data)
--- end)
--- client:set_on_close(function()
---     print("disconnected")
--- end)
--- ```
---
---@param addr string  目标地址，格式 "host:port"，例如 "127.0.0.1:8080"
---@return table client_instance  客户端实例表，失败时抛出 Lua error
function net.client.connect(addr) end

-- ============================================================================
-- 客户端实例方法
-- ============================================================================

--- 发送数据。
---
--- 向已建立的连接发送原始字节数据。连接断开时调用会抛出 error。
---
---@param data string  要发送的原始数据
function client:send(data) end

--- 断开连接并释放资源。
---
--- 主动断开 TCP 连接，释放 Lua 回调引用，并将 C++ 对象的析构
--- 延迟到事件循环的下一个周期执行。调用后该实例不再可用。
---
---@return boolean true  成功断开；false 实例已关闭（无效或重复调用）
function client:disconnect() end

--- 检查连接是否处于活动状态。
---
---@return boolean  true 已连接，false 未连接或已关闭
function client:is_connected() end

--- 设置连接成功回调。
---
--- 回调在 TCP 连接建立成功时触发。通常在 connect 返回后设置，
--- 用于接收连接建立通知并开始发送数据。
---
---@param callback function  回调函数: fun()
function client:set_on_connect(callback) end

--- 设置消息接收回调。
---
--- 回调在收到数据时触发。每次收到的数据可能是任意长度，
--- 不保证与发送方的 send 调用一一对应（TCP 是流协议）。
---
---@param callback function  回调函数: fun(data: string)
function client:set_on_message(callback) end

--- 设置连接关闭回调。
---
--- 回调在连接断开时触发（无论主动断开还是对端关闭）。
--- 注意：手动调用 disconnect() 不会触发此回调。
---
---@param callback function  回调函数: fun()
function client:set_on_close(callback) end
