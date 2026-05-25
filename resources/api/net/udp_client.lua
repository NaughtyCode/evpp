--- UDP Client API
--- Module: net.udp_client
---
--- 基于同步 UDP socket 的 UDP 客户端。
--- 支持两种使用模式:
--- 1. 实例模式: connect() 创建长连接，通过实例方法 send/do_request 操作。
--- 2. 快捷静态方法: do_request() 和 send_to() 无需创建实例，一步完成。
---
--- 所有实例方法均为同步调用（阻塞），适合在协程或非关键路径中使用。

-- ============================================================================
-- net.udp_client 静态方法
-- ============================================================================

--- 创建 UDP 客户端并连接到指定端点。
---
--- 创建一个 UDP 客户端实例，连接到指定的 host:port。
--- 成功后返回客户端实例表；失败时返回 nil 和错误信息。
---
--- 使用示例:
--- ```lua
--- local client = net.udp_client.connect("127.0.0.1", 9000)
--- if not client then
---     print("connect failed")
---     return
--- end
--- client:send("ping")
--- local resp = client:do_request("hello", 5000)
--- client:close()
--- ```
---
---@param host string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port integer  目标端口 (1-65535)
---@return table client_instance  成功: 客户端实例表
---@return nil, string errmsg    失败: 错误信息
function net.udp_client.connect(host, port) end

--- 同步 UDP 请求-响应（快捷方式）。
---
--- 创建临时 UDP 客户端，发送数据并等待响应。
--- 请求完成后自动关闭临时客户端。适合一次性的请求-响应交互。
---
---@param host        string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port        integer  目标端口 (1-65535)
---@param data        string   要发送的数据
---@param timeout_ms? integer  等待响应的超时时间（毫秒），默认 3000，0 表示无限等待
---@return string response  收到的响应数据
function net.udp_client.do_request(host, port, data, timeout_ms) end

--- 单向 UDP 发送（快捷方式）。
---
--- 创建临时 UDP 客户端，发送数据后立即关闭。
--- 不等待响应，适合日志上报、统计打点等 fire-and-forget 场景。
---
---@param host string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port integer  目标端口 (1-65535)
---@param data string   要发送的数据
---@return boolean ok              发送是否成功
---@return string? errmsg          连接失败时的错误信息（仅在 Connect 失败时返回）
function net.udp_client.send_to(host, port, data) end

-- ============================================================================
-- 客户端实例方法
-- ============================================================================

--- 发送数据到已连接的对端。
---
---@param data string  要发送的原始数据
---@return boolean  发送是否成功
function client:send(data) end

--- 同步请求-响应。
---
--- 发送数据并阻塞等待响应。调用前需已通过 connect() 建立连接。
---
---@param data        string   要发送的数据
---@param timeout_ms? integer  等待响应的超时时间（毫秒），默认 3000，0 表示无限等待
---@return string response  收到的响应数据
function client:do_request(data, timeout_ms) end

--- 关闭 UDP 客户端并释放资源。
---
---@return boolean true  成功关闭；false 已关闭
function client:close() end

--- 检查 UDP 客户端是否处于连接状态。
---
---@return boolean  true 已连接，false 未连接或已关闭
function client:is_connected() end
