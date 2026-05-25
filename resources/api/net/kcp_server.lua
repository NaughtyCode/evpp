--- KCP Server API
--- Module: net.kcp_server
---
--- 基于 KCP 协议的可靠 UDP 服务器。
--- 通过 net.kcp_server.listen(port) 创建并启动服务器。
--- 消息在独立的 recv 线程中接收，然后通过 RunInLoop 调度到
--- 事件循环主线程中执行 Lua 回调，确保线程安全。
---
--- KCP 服务器通过会话 ID (conv) 区分不同的客户端会话，
--- 支持暂停/继续接收、运行时替换消息回调，以及 KCP 参数调优。
---
--- KCP 参数说明:
---   nodelay : 是否启用 nodelay 模式，0=禁用, 1=启用
---   interval: 内部更新时钟间隔（毫秒），通常 10-20
---   resend  : 快速重传阈值，0=禁用, 2=2次ACK跳过触发重传
---   nc      : 是否禁用拥塞控制，0=启用, 1=禁用

-- ============================================================================
-- net.kcp_server 静态方法
-- ============================================================================

--- 创建并启动 KCP 服务器，监听指定端口。
---
--- 创建一个 KCP 服务器实例，绑定到指定端口并开始接收数据。
--- 返回服务器实例表。失败时返回 nil 和错误信息。
---
--- 使用示例:
--- ```lua
--- local server = net.kcp_server.listen(9000, function(data, remote_ip, conv)
---     print(string.format("recv from %s conv=%d: %s", remote_ip, conv, data))
--- end)
--- if not server then
---     print("listen failed")
---     return
--- end
--- -- 调优 KCP 参数
--- server:set_kcp_nodelay(1, 10, 2, 1)
--- server:set_kcp_wnd_size(128, 128)
--- ```
---
---@param port_or_ports integer|string  监听端口。整数=单端口；字符串="9000" 或 "9000,9001"（逗号分隔多端口）
---@param on_message?   function        消息接收回调: fun(data: string, remote_ip: string, conv: integer)
---@return table server_instance  成功: 服务器实例表
---@return nil, string errmsg    失败: 错误信息
function net.kcp_server.listen(port_or_ports, on_message) end

-- ============================================================================
-- 服务器实例方法
-- ============================================================================

--- 停止 KCP 服务器并释放所有资源。
---
--- 等待 recv 线程退出后再释放 Lua 引用和 C++ 对象，
--- 确保不会有残留的消息回调在 stop 返回后触发。
---
---@return boolean true  成功停止；false 已停止
function server:stop() end

--- 暂停接收 KCP 消息。
---
--- 暂停后 recv 线程停止读取数据，但服务器仍保持运行状态。
--- 已进入事件循环队列的消息仍会正常触发回调。
function server:pause() end

--- 继续接收 KCP 消息（恢复暂停的服务器）。
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
---@param callback function  消息接收回调: fun(data: string, remote_ip: string, conv: integer)
function server:set_on_message(callback) end

-- ============================================================================
-- KCP 参数调优方法
-- ============================================================================

--- 设置 KCP nodelay 参数。
---
--- 常见配置:
---   普通模式:  set_kcp_nodelay(0, 40, 0, 0)  -- 关闭 nodelay
---   快速模式:  set_kcp_nodelay(1, 10, 2, 1)  -- 启用 nodelay, 10ms 时钟, 2次快速重传, 禁用拥塞控制
---   极速模式:  set_kcp_nodelay(2, 10, 2, 1)  -- 启用 nodelay + 流速控制
---
---@param nodelay  integer  是否启用 nodelay: 0=禁用, 1=启用, 2=启用+流速控制
---@param interval integer  内部更新时钟间隔（毫秒），取值范围 10-5000
---@param resend   integer  快速重传阈值: 0=禁用, >=2 表示跳过 N 次 ACK 后直接重传
---@param nc       integer  是否禁用拥塞控制: 0=启用拥塞控制, 1=禁用（流速全开）
function server:set_kcp_nodelay(nodelay, interval, resend, nc) end

--- 设置 KCP 窗口大小。
---
--- 窗口越大，吞吐量越高，但内存占用和延迟也会增加。
---
---@param sndwnd integer  发送窗口大小（单位: KCP 包个数），默认 32
---@param rcvwnd integer  接收窗口大小（单位: KCP 包个数），默认 128
function server:set_kcp_wnd_size(sndwnd, rcvwnd) end

--- 设置 KCP 最大传输单元（MTU）。
---
--- 值越大单包可承载的数据越多，但超过网络 MTU 会导致 IP 分片。
--- 纯 UDP 环境通常设置 1400。
---
---@param mtu integer  MTU 值（字节），默认 1400，范围 50 ~ 1500
function server:set_kcp_mtu(mtu) end

--- 设置会话超时时间。
---
--- 当某个 conv 会话在此时间内没有数据交互时，服务器会自动清理该会话。
---
---@param timeout_ms integer  会话超时时间（毫秒），0 ~ 2^32-1
function server:set_session_timeout(timeout_ms) end
