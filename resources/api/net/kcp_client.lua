--- KCP Client API
--- Module: net.kcp_client
---
--- 基于 KCP 协议的可靠 UDP 客户端。
--- KCP 是一个快速可靠的 ARQ 协议，在不可靠的 UDP 链路上提供
--- 可靠传输，适合对延迟敏感的应用场景（如游戏、实时通信）。
---
--- 支持两种使用模式:
--- 1. 两步创建: new() 创建未连接实例 → 调优参数 → connect() 连接
--- 2. 一步创建: connect(host, port) 直接连接（使用默认 KCP 参数）
---
--- 快捷静态方法 do_request() 支持一次性请求-响应。
--- 所有方法均为同步调用（阻塞）。
---
--- KCP 参数说明:
---   nodelay : 是否启用 nodelay 模式，0=禁用, 1=启用
---   interval: 内部更新时钟间隔（毫秒），通常 10-20
---   resend  : 快速重传阈值，0=禁用, 2=2次ACK跳过触发重传
---   nc      : 是否禁用拥塞控制，0=启用, 1=禁用
---   conv    : 会话 ID，通信双方必须一致

-- ============================================================================
-- net.kcp_client 静态方法
-- ============================================================================

--- 创建一个未连接的 KCP 客户端实例。
---
--- 创建一个 KCP 客户端但不立即连接，适合在连接前调优 KCP 参数。
--- 调优完成后调用 instance:connect(host, port) 建立连接。
---
--- 使用示例:
--- ```lua
--- local client = net.kcp_client.new(12345)
--- client:set_kcp_nodelay(1, 10, 2, 1)  -- 快速模式
--- client:set_kcp_wnd_size(128, 128)
--- client:set_kcp_mtu(1400)
--- client:connect("127.0.0.1", 9000)
--- client:send("hello")
--- ```
---
---@param conv? integer  会话 ID (0 ~ 2^32-1)，默认 0x11223344。通信双方必须使用相同的 conv
---@return table client_instance  客户端实例表
function net.kcp_client.new(conv) end

--- 创建 KCP 客户端并直接连接（一步完成）。
---
--- 创建 KCP 客户端实例并立即连接到目标端点。
--- 使用默认 KCP 参数（或自定义 conv）。
--- 成功返回实例表；失败返回 nil 和错误信息。
---
---@param host  string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port  integer  目标端口 (1-65535)
---@param conv? integer  会话 ID (0 ~ 2^32-1)，默认 0x11223344
---@return table client_instance  成功: 客户端实例表
---@return nil, string errmsg    失败: 错误信息
function net.kcp_client.connect(host, port, conv) end

--- 同步 KCP 请求-响应（快捷方式）。
---
--- 创建临时 KCP 客户端，发送数据并等待响应。
--- 请求完成后自动关闭临时客户端。
---
---@param host        string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port        integer  目标端口 (1-65535)
---@param data        string   要发送的数据
---@param timeout_ms? integer  等待响应的超时时间（毫秒），默认 3000，0 表示无限等待
---@param conv?       integer  会话 ID (0 ~ 2^32-1)，默认 0x11223344
---@return string response  收到的响应数据
function net.kcp_client.do_request(host, port, data, timeout_ms, conv) end

-- ============================================================================
-- 客户端实例方法
-- ============================================================================

--- 连接实例到目标端点（用于 new() 创建的未连接实例）。
---
--- 调用前可使用 set_kcp_* 系列方法调优参数。
---
---@param host string   目标主机 IP 地址，例如 "127.0.0.1"
---@param port integer  目标端口 (1-65535)
---@return boolean true       连接成功
---@return boolean false, string errmsg  连接失败
function client:connect(host, port) end

--- 发送数据。
---
---@param data string  要发送的原始数据
---@return boolean  发送是否成功
function client:send(data) end

--- 同步请求-响应。
---
--- 发送数据并阻塞等待响应。
---
---@param data        string   要发送的数据
---@param timeout_ms? integer  等待响应的超时时间（毫秒），默认 3000，0 表示无限等待
---@return string response  收到的响应数据
function client:do_request(data, timeout_ms) end

--- 关闭 KCP 客户端并释放资源。
---
---@return boolean true  成功关闭；false 已关闭
function client:close() end

--- 检查 KCP 客户端是否处于连接状态。
---
---@return boolean  true 已连接，false 未连接或已关闭
function client:is_connected() end

-- ============================================================================
-- KCP 参数调优方法（必须在 connect() 之前调用）
-- ============================================================================

--- 设置 KCP nodelay 参数。
---
--- 必须在 connect() 之前调用。用于控制 KCP 的传输模式。
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
function client:set_kcp_nodelay(nodelay, interval, resend, nc) end

--- 设置 KCP 窗口大小。
---
--- 必须在 connect() 之前调用。
--- 窗口越大，吞吐量越高，但内存占用和延迟也会增加。
---
---@param sndwnd integer  发送窗口大小（单位: KCP 包个数），默认 32
---@param rcvwnd integer  接收窗口大小（单位: KCP 包个数），默认 128
function client:set_kcp_wnd_size(sndwnd, rcvwnd) end

--- 设置 KCP 最大传输单元（MTU）。
---
--- 必须在 connect() 之前调用。
--- 值越大单包可承载的数据越多，但超过网络 MTU 会导致 IP 分片。
--- 纯 UDP 环境通常设置 1400。
---
---@param mtu integer  MTU 值（字节），默认 1400，范围 50 ~ 1500
function client:set_kcp_mtu(mtu) end

--- 设置 KCP 会话 ID。
---
--- 必须在 connect() 之前调用（仅对 new() 创建的实例有意义）。
--- 通信双方的 conv 必须一致，同一 UDP 端口上通过 conv 区分不同会话。
---
---@param conv integer  会话 ID (0 ~ 2^32-1)
function client:set_kcp_conv(conv) end
