--- Network API
--- 全局模块: net
---
--- 子模块:
---   net.client  — TCP 客户端
---   net.server  — TCP 服务端
---   net.http    — HTTP 客户端
---
--- 所有回调在事件循环线程上异步调用。
--- 回调签名汇总:
---   client.on_connect()                                — 无参数
---   client.on_message(data: string)                    — 原始数据
---   client.on_close()                                  — 无参数
---   server.on_connect(conn_id: integer, addr: string)  — 新连接
---   server.on_message(conn_id: integer, data: string)  — 服务端级默认
---   server.on_close(conn_id: integer, addr: string)    — 服务端级默认
---   连接级 on_message(data: string)                    — 覆盖服务端默认
---   连接级 on_close(conn_id: integer, addr: string)    — 覆盖服务端默认
---   http.on_response(http_code: integer, body: string) — 响应/超时回调

-- ============================================================================
-- net.client — TCP 客户端
-- ============================================================================

--- 创建 TCP 客户端并连接到 host:port。
---@param addr        string   "host:port" 格式的地址
---@param on_connect? function 连接成功回调: fun()
---@param on_message? function 收到数据回调: fun(data: string)
---@param on_close?   function 连接关闭回调: fun()
---@return integer client_id  客户端标识，用于后续操作
function net.client.connect(addr, on_connect, on_message, on_close) end

--- 向连接发送数据。
---@param client_id integer  connect 返回的客户端 id
---@param data      string  要发送的原始数据
function net.client.send(client_id, data) end

--- 断开连接，释放回调。
---@param client_id integer
---@return boolean existed  找到并断开返回 true，未找到返回 false
function net.client.disconnect(client_id) end

--- 查询连接是否活跃。
---@param client_id integer
---@return boolean connected
function net.client.is_connected(client_id) end

--- 替换消息回调。
---@param client_id integer
---@param callback  function  收到数据回调: fun(data: string)
function net.client.set_on_message(client_id, callback) end

--- 替换关闭回调。
---@param client_id integer
---@param callback  function  连接关闭回调: fun()
function net.client.set_on_close(client_id, callback) end

-- ============================================================================
-- net.server — TCP 服务端
-- ============================================================================

--- 创建并启动 TCP 服务端，监听 host:port。
---@param addr        string   "host:port" 格式的地址
---@param on_connect? function 新连接回调: fun(conn_id: integer, remote_addr: string)
---@param on_message? function 服务端级数据回调: fun(conn_id: integer, data: string)
---@param on_close?   function 服务端级关闭回调: fun(conn_id: integer, remote_addr: string)
---@return integer server_id  成功: 服务端标识
---@return nil, string errmsg  失败: 错误信息
function net.server.listen(addr, on_connect, on_message, on_close) end

--- 向指定连接发送数据。
---@param conn_id integer  连接 id
---@param data    string   要发送的原始数据
function net.server.send(conn_id, data) end

--- 关闭指定连接并释放其回调。
---@param conn_id integer
---@return boolean closed  找到并关闭返回 true，未找到返回 false
function net.server.close_conn(conn_id) end

--- 停止服务端，释放所有连接和回调。
---@param server_id integer
---@return boolean stopped  找到并停止返回 true，未找到返回 false
function net.server.stop(server_id) end

--- 为指定连接设置独立的消息回调，覆盖服务端默认 on_message。
--- 传入 nil 则移除独立回调，恢复使用服务端默认。
---@param conn_id  integer   连接 id
---@param callback function?  收到数据回调: fun(data: string)
function net.server.set_on_message(conn_id, callback) end

--- 为指定连接设置独立的关闭回调，覆盖服务端默认 on_close。
--- 传入 nil 则移除独立回调，恢复使用服务端默认。
---@param conn_id  integer   连接 id
---@param callback function?  连接关闭回调: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_close(conn_id, callback) end

--- 替换服务端的 on_connect 回调。
---@param server_id integer   服务端 id
---@param callback  function?  新连接回调: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_connect(server_id, callback) end

--- 替换服务端的 on_close 回调。
---@param server_id integer   服务端 id
---@param callback  function?  连接关闭回调: fun(conn_id: integer, remote_addr: string)
function net.server.set_on_disconnect(server_id, callback) end

-- ============================================================================
-- net.http — HTTP 客户端
-- ============================================================================

--- 发起异步 HTTP GET 请求 (10 秒超时)。
---@param url         string   请求 URL
---@param on_response function 响应回调: fun(http_code: integer, body: string)
---                             出错/超时时 http_code 为 0，body 为 ""
function net.http.get(url, on_response) end

--- 发起异步 HTTP POST 请求 (10 秒超时)。
---@param url         string   请求 URL
---@param body        string   请求体
---@param on_response function 响应回调: fun(http_code: integer, body: string)
function net.http.post(url, body, on_response) end
