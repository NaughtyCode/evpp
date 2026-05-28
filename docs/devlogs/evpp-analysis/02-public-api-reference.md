# evpp Lua API 参考

本文档由 C++ 绑定代码直接交叉验证，准确反映导出到 Lua VM 的实际 API。

## 设计约定

- 所有网络/IO 对象使用 **instance table** 模式（非 ID 模式）
- 回调通过 `set_on_*` 方法设置在 instance table 上
- instance table 的生命周期由 Lua GC（`__gc`）管理
- 全局函数通过 `function` 调用；实例方法通过 `instance:method()` 调用

---
## 1. net.client — TCP 异步客户端

```lua
-- 连接服务器，返回 client 实例（失败返回 nil, errmsg）
local client = net.client.connect("127.0.0.1:8080")

-- 设置回调
client:set_on_connect(function()
    print("connected")
end)
client:set_on_message(function(data)
    print("received: " .. data)
end)
client:set_on_close(function()
    print("disconnected")
end)

-- 发送数据（自动添加长度前缀帧）
client:send("hello world")

-- 查询连接状态
local ok = client:is_connected()

-- 断开连接
client:disconnect()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.client.connect` | `(addr) → instance \| nil, err` | 连接到 addr（"host:port"），立即返回 |
| `c:send` | `(data)` | 发送数据（使用 length-prefixed framing） |
| `c:disconnect` | `() → bool` | 断开连接，清理资源 |
| `c:is_connected` | `() → bool` | 是否处于连接状态 |
| `c:set_on_connect` | `(fn \| nil)` | 连接建立时回调 `fn()` |
| `c:set_on_message` | `(fn \| nil)` | 收到消息时回调 `fn(data)` |
| `c:set_on_close` | `(fn \| nil)` | 连接关闭时回调 `fn()` |

---
## 2. net.server — TCP 多线程服务端

```lua
-- 开始监听，返回 server 实例
local server = net.server.listen("0.0.0.0:9099")

-- 设置回调
server:set_on_connect(function(conn, remote_addr)
    print("new conn from " .. remote_addr)
    conn:set_on_message(function(data)
        print("msg: " .. data)
        conn:send(data)  -- echo
    end)
    conn:set_on_close(function()
        print("conn closed")
    end)
end)

-- 停止服务器
server:stop()
```

### Server 实例方法

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.server.listen` | `(addr) → instance \| nil, err` | 在 addr 上监听（thread_num=0，主线程处理） |
| `s:stop` | `() → bool` | 停止服务器 |
| `s:set_on_connect` | `(fn \| nil)` | 新连接回调 `fn(conn_inst, remote_addr)` |
| `s:set_on_message` | `(fn \| nil)` | 接收消息回调 `fn(conn_inst, data)` |
| `s:set_on_close` | `(fn \| nil)` | 连接关闭回调 `fn(conn_inst, remote_addr)` |

### Connection 实例方法

| 函数 | 签名 | 说明 |
|---|---|---|
| `conn:send` | `(data)` | 发送数据（使用 length-prefixed framing） |
| `conn:close` | `() → bool` | 关闭此连接 |
| `conn:is_connected` | `() → bool` | 连接是否活跃 |
| `conn:set_on_message` | `(fn \| nil)` | 此连接的消息回调 `fn(data)` |
| `conn:set_on_close` | `(fn \| nil)` | 此连接的关闭回调 `fn(remote_addr)` |

---
## 3. net.http — HTTP 异步客户端

```lua
-- GET 请求
net.http.get("http://example.com/api/data",
    function(code, body)
        print("HTTP " .. code .. ": " .. body)
    end)

-- POST 请求
net.http.post("http://example.com/api/data", "post body",
    function(code, body)
        print("HTTP " .. code .. ": " .. body)
    end)
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.http.get` | `(url, callback)` | GET 请求，超时由服务端配置控制 |
| `net.http.post` | `(url, body, callback)` | POST 请求 |

回调签名: `function(http_code, body_string)`

---
## 4. net.udp_client — UDP 同步客户端

```lua
-- 创建连接
local c = net.udp_client.connect("127.0.0.1", 5353)

-- 发送（fire-and-forget）
local ok = c:send("hello")

-- 发送并等待响应
local resp = c:do_request("ping", 3000)  -- 3 秒超时

-- 一次性请求（无需预先连接）
local resp = net.udp_client.do_request("127.0.0.1", 5353, "ping", 3000)

-- 一次性发送
net.udp_client.send_to("127.0.0.1", 5353, "data")

c:close()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.udp_client.connect` | `(host, port) → instance \| nil, err` | 创建 UDP 客户端 |
| `c:send` | `(data) → bool` | 发送数据 |
| `c:do_request` | `(data, timeout_ms) → string` | 发送并同步等待响应 |
| `c:close` | `() → bool` | 关闭连接 |
| `c:is_connected` | `() → bool` | 连接状态 |
| `net.udp_client.do_request` | `(host, port, data, timeout_ms) → string` | 一次性请求 |
| `net.udp_client.send_to` | `(host, port, data) → bool` | 一次性发送 |

---
## 5. net.udp_server — UDP 服务端

```lua
-- 单端口监听
local s = net.udp_server.listen(5353, function(data, remote_ip)
    print("from " .. remote_ip .. ": " .. data)
end)

-- 多端口监听
local s = net.udp_server.listen("5353,5354", callback)

s:pause()
s:continue()
s:stop()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.udp_server.listen` | `(port_or_ports, on_message) → instance` | 开始监听，port 可以是数字或 "p1,p2" |
| `s:stop` | `() → bool` | 停止服务器 |
| `s:pause` | `()` | 暂停接收 |
| `s:continue` | `()` | 继续接收 |
| `s:is_running` | `() → bool` | 是否正在运行 |
| `s:set_on_message` | `(fn \| nil)` | 消息回调 `fn(data, remote_ip)` |

---
## 6. net.kcp_client — KCP 可靠 UDP 客户端

```lua
-- 方式一：直接连接
local c = net.kcp_client.connect("127.0.0.1", 9099, conv_id)

-- 方式二：先创建实例，调优后再连接
local c = net.kcp_client.new(conv_id)
c:set_kcp_nodelay(1, 10, 2, 1)
c:set_kcp_wnd_size(128, 128)
c:set_kcp_mtu(1400)
c:connect("127.0.0.1", 9099)

-- 通信
c:send("hello")
local resp = c:do_request("ping", 3000)

-- 一次性请求
local resp = net.kcp_client.do_request("127.0.0.1", 9099, "ping", 3000, conv_id)

c:close()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.kcp_client.new` | `([conv]) → instance` | 创建未连接的 KCP 客户端 |
| `net.kcp_client.connect` | `(host, port[, conv]) → instance \| nil, err` | 创建并连接 |
| `net.kcp_client.do_request` | `(host, port, data, timeout[, conv]) → string` | 一次性请求 |
| `c:connect` | `(host, port) → bool` | 连接（用于 new() 创建的实例） |
| `c:send` | `(data) → bool` | 发送数据 |
| `c:do_request` | `(data, timeout_ms) → string` | 发送并等待响应 |
| `c:close` | `() → bool` | 关闭 |
| `c:is_connected` | `() → bool` | 连接状态 |
| `c:set_kcp_nodelay` | `(nodelay, interval, resend, nc)` | 设置 KCP nodelay 参数 |
| `c:set_kcp_wnd_size` | `(sndwnd, rcvwnd)` | 设置窗口大小 |
| `c:set_kcp_mtu` | `(mtu)` | 设置 MTU |
| `c:set_kcp_conv` | `(conv)` | 设置会话 ID |

---
## 7. net.kcp_server — KCP 可靠 UDP 服务端

```lua
local s = net.kcp_server.listen(9099, function(data, remote_ip, conv)
    print("from " .. remote_ip .. " conv=" .. conv .. ": " .. data)
end)

-- 调优（需在 listen 后设置）
s:set_kcp_nodelay(1, 10, 2, 1)
s:set_kcp_wnd_size(128, 128)
s:set_kcp_mtu(1400)
s:set_session_timeout(60000)  -- ms

s:pause()
s:continue()
s:stop()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `net.kcp_server.listen` | `(port_or_ports[, on_message]) → instance` | 开始监听 |
| `s:stop` | `() → bool` | 停止服务器 |
| `s:pause` | `()` | 暂停接收 |
| `s:continue` | `()` | 继续接收 |
| `s:is_running` | `() → bool` | 是否正在运行 |
| `s:set_on_message` | `(fn \| nil)` | 消息回调 `fn(data, remote_ip, conv)` |
| `s:set_kcp_nodelay` | `(nodelay, interval, resend, nc)` | 设置 KCP nodelay |
| `s:set_kcp_wnd_size` | `(sndwnd, rcvwnd)` | 设置窗口大小 |
| `s:set_kcp_mtu` | `(mtu)` | 设置 MTU |
| `s:set_session_timeout` | `(timeout_ms)` | 设置会话超时 |

---
## 8. timer — 定时器模块

```lua
-- 一次性延迟（ms）
local tid = timer.timeout(1000, function()
    print("fired after 1s")
end)

-- 周期性定时器
local tid = timer.interval(500, function()
    print("every 500ms")
end)

-- 取消
timer.cancel(tid)
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `timer.timeout` | `(ms, callback) → timer_id` | 一次性延迟，ms ∈ [1, ~INT64_MAXns] |
| `timer.interval` | `(ms, callback) → timer_id` | 周期性定时器 |
| `timer.cancel` | `(timer_id) → true \| nil, err` | 取消定时器 |

---
## 9. 日志函数（全局）

```lua
log_trace("trace message")
log_debug("debug message")
log_info("info message")
log_warn("warning message")
log_error("error message")
log_fatal("fatal message")
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `log_trace` | `(msg)` | TRACE 级别 |
| `log_debug` | `(msg)` | DEBUG 级别 |
| `log_info` | `(msg)` | INFO 级别 |
| `log_warn` | `(msg)` | WARN 级别 |
| `log_error` | `(msg)` | ERROR 级别 |
| `log_fatal` | `(msg)` | CRITICAL 级别 |

---
## 10. cmsgpack — MessagePack 编解码

```lua
-- 编码（接受多个参数，返回二进制字符串）
local bin = cmsgpack.pack({key = "value"}, 42, true, nil)

-- 解码全部
local vals = {cmsgpack.unpack(bin)}

-- 解码一个
local val, next_offset = cmsgpack.unpack_one(bin, start_offset)

-- 解码指定数量
local vals, next_offset = cmsgpack.unpack_limit(bin, count, start_offset)
```

同时提供 `cmsgpack_safe` 模块，函数签名相同但错误返回 `nil, errmsg` 而非抛出异常。

| 函数 | 签名 | 说明 |
|---|---|---|
| `cmsgpack.pack` | `(...) → string` | 将 Lua 值编码为 MessagePack |
| `cmsgpack.unpack` | `(data) → ...` | 解码全部值 |
| `cmsgpack.unpack_one` | `(data[, offset]) → val, next_offset` | 解码一个值；next_offset=-1 表示结束 |
| `cmsgpack.unpack_limit` | `(data, limit[, offset]) → ..., next_offset` | 解码指定数量 |

支持的 Lua 类型映射: string→str, integer(fixnum)→int, float→float/double, boolean→bool, nil→nil, table→array/map

---
## 11. entity — 实体模块

```lua
-- 创建实体（自动激活）
local e = entity.create()         -- 自动分配 ID
local e = entity.create(eid)      -- 指定 ID

-- 属性操作
e:set_attr("hp", 100)
e:set_attr("name", "player1")
e:set_attr("alive", true)
local hp = e:get_attr("hp")
local has = e:has_attr("hp")

-- 状态管理
local state = e:get_state()  -- "created" | "active" | "suspended" | "destroyed"
e:activate()
e:suspend()

-- 网络绑定
e:bind_connection(conn)    -- 绑定 TCP 连接
local conn = e:get_connection()
e:send("data")             -- 通过绑定的连接发送

-- 组件系统
e:add_component("name", component_table)
local comp = e:get_component("name")
e:remove_component("name")

-- 实体定时器
local tid = e:add_timer(1000, false, function()  -- 一次性
    print("entity timer")
end)
e:cancel_timer(tid)

e:destroy()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `entity.create` | `([id]) → instance` | 创建并激活实体 |
| `e:destroy` | `() → bool` | 销毁实体 |
| `e:get_id` | `() → integer` | 获取实体 ID |
| `e:get_state` | `() → string` | 获取状态: created/active/suspended/destroyed |
| `e:activate` | `()` | 激活实体 |
| `e:suspend` | `()` | 挂起实体 |
| `e:get_attr` | `(key) → value` | 获取属性值 |
| `e:set_attr` | `(key, value)` | 设置属性（支持 int/float/string/bool） |
| `e:has_attr` | `(key) → bool` | 是否存在属性 |
| `e:bind_connection` | `(conn \| nil)` | 绑定/解绑网络连接 |
| `e:get_connection` | `() → conn \| nil` | 获取绑定的连接 |
| `e:send` | `(data)` | 通过绑定连接发送数据 |
| `e:add_timer` | `(ms, repeat, fn) → timer_id` | 添加实体定时器（实体 suspend 时自动暂停） |
| `e:cancel_timer` | `(timer_id)` | 取消实体定时器 |
| `e:add_component` | `(name, table)` | 添加 Lua 组件 |
| `e:get_component` | `(name) → table \| nil` | 获取组件 |
| `e:remove_component` | `(name)` | 移除组件 |

---
## 12. space — 空间模块

```lua
-- 创建空间
local sid = space.create("Level_01", {
    max_entities = 1000,
    max_players = 100,
    scripts = {"level_init.lua"},
})

-- 查询
local info = space.get(sid)     -- {id, name, entity_count}
local list = space.list()       -- [{id, name, entity_count}, ...]
local cur = space.current()     -- 默认空间信息

-- 跨空间消息
space.send(target_space, target_entity, payload)

-- 轮询接收
local msg = space.poll()  -- {source_space, source_entity, target_entity, payload}

space.destroy(sid)
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `space.create` | `(name[, config]) → space_id \| nil, err` | 创建空间 |
| `space.get` | `(space_id) → info \| nil` | 获取空间信息 |
| `space.destroy` | `(space_id)` | 销毁空间 |
| `space.send` | `(space_id, entity_id, payload)` | 发送跨空间消息 |
| `space.list` | `() → array` | 列出所有空间 |
| `space.current` | `() → info \| nil` | 获取默认空间信息 |
| `space.poll` | `() → msg \| nil` | 轮询待处理消息 |

---
## 13. aoi — AOI（Area of Interest）模块

```lua
-- 初始化网格
aoi.init(world_width, world_height, cell_size)

-- 设置事件回调
aoi.set_event_callback(function(observer_id, target_id, entered)
    if entered then
        print(observer_id .. " sees " .. target_id)
    else
        print(observer_id .. " lost " .. target_id)
    end
end)

-- 实体管理
aoi.register_entity(entity_id, x, y, aoi_radius)
aoi.update_entity(entity_id, new_x, new_y)
aoi.unregister_entity(entity_id)

-- 查询
local visible = aoi.get_visible(entity_id)        -- {entity_id, ...}
local nearby = aoi.query_radius(x, y, radius)    -- {entity_id, ...}
local count = aoi.count()

aoi.shutdown()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `aoi.init` | `(world_w, world_h[, cell_size]) → bool` | 初始化空间网格 |
| `aoi.set_event_callback` | `(fn) → bool \| nil, err` | 设置 AOI 进入/离开回调 |
| `aoi.register_entity` | `(entity_id, x, y[, radius])` | 注册实体到 AOI |
| `aoi.update_entity` | `(entity_id, x, y)` | 更新实体位置 |
| `aoi.unregister_entity` | `(entity_id)` | 从 AOI 移除实体 |
| `aoi.get_visible` | `(entity_id) → array` | 获取可见实体 ID 列表 |
| `aoi.query_radius` | `(x, y, radius) → array` | 范围查询 |
| `aoi.count` | `() → integer` | 已注册实体数 |
| `aoi.shutdown` | `()` | 关闭 AOI 系统 |

---
## 14. auth — 认证模块

```lua
-- Token 后端
auth.set_token_backend()
auth.add_token("secret_token", "entity_001")

-- JWT 后端
auth.set_jwt_backend("jwt_secret_key")

-- 权限管理
auth.grant_permission("entity_001", "admin.read")
auth.revoke_permission("entity_001", "admin.read")
local has = auth.has_permission("entity_001", "admin.read")

-- 会话管理
local ok, entity_id, session_id = auth.authenticate("token", {token = "secret"})
local sid = auth.create_session("entity_001")
local valid = auth.validate_session(sid)
auth.revoke_session(sid)
auth.cleanup_expired()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `auth.set_token_backend` | `() → bool` | 初始化 token 后端 |
| `auth.set_jwt_backend` | `(secret) → bool` | 初始化 JWT 后端 |
| `auth.add_token` | `(token, entity_id) → bool` | 添加 token |
| `auth.authenticate` | `(method, params) → ok, entity_id, session_id \| nil, err` | 认证 |
| `auth.create_session` | `(entity_id) → session_id \| nil` | 创建会话 |
| `auth.validate_session` | `(session_id) → bool` | 验证会话 |
| `auth.revoke_session` | `(session_id) → bool` | 吊销会话 |
| `auth.grant_permission` | `(entity_id, permission) → bool` | 授予权限 |
| `auth.revoke_permission` | `(entity_id, permission) → bool` | 撤销权限 |
| `auth.has_permission` | `(entity_id, permission) → bool` | 检查权限 |
| `auth.cleanup_expired` | `()` | 清理过期会话 |

---
## 15. rpc — RPC 模块

```lua
-- 服务端
local server = rpc.new_server()
server:register_service("math", function(service, method, body)
    if method == "add" then
        return '{"result": ' .. (a + b) .. '}'
    end
    return "{}"
end)
server:unregister_service("math")
server:stop()

-- 客户端
local client = rpc.new_client()
client:set_send_callback(function(msgid, service, method, body)
    -- 由传输层发送此消息
    transport:send(serialize(msgid, service, method, body))
end)

-- 同步调用
local result, err = client:call("math", "add", '{"a":1,"b":2}', 5000)

-- 异步调用
client:call_async("math", "add", '{"a":1,"b":2}', function(body, err)
    if err then
        log_error(err)
    else
        print("result: " .. body)
    end
end)

client:stop()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `rpc.new_server` | `() → instance` | 创建 RPC 服务器 |
| `server:register_service` | `(name, handler_fn) → bool \| nil, err` | 注册服务回调 |
| `server:unregister_service` | `(name) → bool \| nil, err` | 取消注册服务 |
| `server:stop` | `() → bool` | 停止服务器 |
| `rpc.new_client` | `() → instance` | 创建 RPC 客户端 |
| `client:call` | `(svc, method, args, timeout) → body, nil \| nil, err` | 同步调用 |
| `client:call_async` | `(svc, method, args, callback) → bool` | 异步调用，回调 `fn(body, nil)` 或 `fn(nil, err)` |
| `client:set_send_callback` | `(fn) → bool \| nil, err` | 设置传输层回调 `fn(msgid, svc, method, body)` |
| `client:stop` | `() → bool` | 停止客户端 |

> **注意**: RPC 模块需要在主循环中调用 `UpdateRpcBindings()` 来处理待处理请求和响应。

---
## 16. orm — ORM 模块（需 ENGINE_MONGODB_ENABLED）

```lua
-- 定义 Schema
orm.define("players", {
    fields = {
        name = "string",
        score = "int",
        health = "double",
        active = "bool",
    },
    indexes = {
        {fields = {"name"}, unique = true},
        {fields = {"score"}},
    },
})

-- CRUD 操作
orm.insert("players", '{"name":"alice","score":100}')
local docs = orm.find("players", {name = "alice"})  -- → JSON string 数组
local doc = orm.find_by_id("players", "id_here")     -- → JSON string or nil
orm.update("players", "id_here", '{"score":200}')
orm.delete("players", "id_here")

-- 缓存统计
local stats = orm.cache_stats()  -- {hits=N, misses=N, hit_rate=F}
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `orm.define` | `(collection, schema) → bool` | 注册集合 Schema |
| `orm.find` | `(collection, query) → array` | 查询（返回 JSON string 数组） |
| `orm.find_by_id` | `(collection, id) → json \| nil` | 按 ID 查询 |
| `orm.insert` | `(collection, json) → bool` | 插入文档 |
| `orm.update` | `(collection, id, json) → bool` | 更新文档 |
| `orm.delete` | `(collection, id) → bool` | 删除文档 |
| `orm.cache_stats` | `() → table` | 缓存统计 |

---
## 17. db_service — 数据库服务（需 ENGINE_MONGODB_ENABLED）

```lua
-- 状态查询
local running = db_is_running()
local healthy = db_is_healthy()
local threads = db_get_thread_count()

-- 发送请求
local ok = db_send_request({
    request_id = 1001,
    operation = "find",
    database = "game_db",
    collection = "players",
    bson_data = '{"score": {"$gt": 100}}',
    limit = 10,
})

-- 轮询响应
local resp = db_poll_response()
-- {request_id, success, error_code, error_message, result_data, affected_count}
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `db_is_running` | `() → bool` | 数据库服务是否运行中 |
| `db_is_healthy` | `() → bool` | 所有 DB 线程是否健康 |
| `db_get_thread_count` | `() → int` | DB 工作线程数 |
| `db_send_request` | `(req_table) → bool` | 发送异步数据库请求 |
| `db_poll_response` | `() → table \| nil` | 非阻塞轮询响应 |

**支持的操作**: `find`, `find_one`, `insert_one`, `insert_many`, `update_one`, `update_many`, `delete_one`, `delete_many`, `count`, `aggregate`, `command`, `execute_script`, `noop`

---
## 18. import — 模块导入

```lua
-- 导入模块
import("game.player")
local mymod = import("shared.utils")

-- 路径管理
import.setpath("/scripts/?.lua;/libs/?.lua")
import.addpath("/mods/?.lua")

-- 查看已加载模块
local loaded = import.loaded()

-- 清除缓存
import.clearcache()
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `import(modname)` | `(name) → module` | 导入模块（类似 require） |
| `import.setpath` | `(paths)` | 设置搜索路径 |
| `import.addpath` | `(path)` | 添加搜索路径 |
| `import.loaded` | `() → table` | 返回已加载模块的副本 |
| `import.clearcache` | `()` | 清除模块缓存 |

---
## 19. mem — 内存统计模块（需 ENGINE_MEM_STATS_ENABLED）

```lua
if mem.is_enabled() then
    local stats = mem.get_stats()
    -- {
    --   totals = {alloc_count, free_count, alloc_bytes, free_bytes,
    --             bytes_in_use, peak_bytes, peak_alloc_count},
    --   by_operation = {new = {...}, malloc = {...}, ...},
    --   size_buckets = {lt_64 = {count, bytes}, ...},
    --   recent_allocs = {{file, line, size, op}, ...},
    --   active_alloc_count = N,
    -- }
    mem.dump_stats("/path/to/stats.json")
    mem.reset_stats()
end
```

| 函数 | 签名 | 说明 |
|---|---|---|
| `mem.is_enabled` | `() → bool` | 内存统计是否启用 |
| `mem.get_stats` | `() → table` | 获取详细内存统计 |
| `mem.reset_stats` | `()` | 重置统计计数器 |
| `mem.dump_stats` | `(filepath) → bool` | 导出统计到文件 |

---
## 20. mongo — MongoDB 底层驱动（需 ENGINE_MONGODB_ENABLED）

提供 `bson` 和 `mongoc` 两个模块，直接封装 mongo-c-driver 的 C API。

### bson 模块
```lua
local doc = bson.Document.new()          -- 创建 BSON 文档
doc:append_int32("hp", 100)
doc:append_utf8("name", "alice")
local b = doc:to_bson()                  -- 序列化

local iter = bson.Iterator.new(b)        -- 遍历
while iter:next() do
    local key = iter:key()
    local vtype = iter:type()
end

local arr = bson.Vector.new()            -- BSON 数组构建器
arr:push_int32(1)
arr:push_utf8("hello")

local oid = bson.Oid.new("507f1f77bcf86cd799439011")
local json = bson.Ext.as_json(b)
```

### mongoc 模块
```lua
local uri = mongoc.Uri.new("mongodb://localhost:27017")
local client = mongoc.Client.new(uri)
local db = client:get_database("game_db")
local col = db:get_collection("players")

local cursor = col:find(bson.Document.new():append_int32("score", 100):to_bson())
while cursor:next() do
    local doc = cursor:current()
    -- 处理文档
end

local pool = mongoc.ClientPool.new(uri)  -- 连接池
local result = col:insert_one(doc)
local session = client:start_session()
local cs = db:watch()                     -- Change Stream
```

> **建议**: 大多数情况下使用 `orm` 和 `db_service` 模块，它们提供了更简单、更安全的抽象。

---
## 模块注册总览

| 全局名 | 类型 | 条件 | 说明 |
|---|---|---|---|
| `net` | table | 始终 | 网络模块容器（7 个子模块） |
| `entity` | table | 始终 | 实体创建与管理 |
| `timer` | table | 始终 | 定时器 |
| `log_trace`..`log_fatal` | 函数 | 始终 | 日志输出（6 个全局函数） |
| `cmsgpack` | table | 始终 | MessagePack 编解码 |
| `cmsgpack_safe` | table | 始终 | MessagePack（安全错误返回） |
| `space` | table | 始终 | 空间管理 |
| `aoi` | table | 始终 | 兴趣区域管理 |
| `auth` | table | 始终 | 认证与权限 |
| `rpc` | table | 始终 | RPC 客户端/服务端 |
| `import` | table/callable | 始终 | 模块导入系统 |
| `orm` | table | MongoDB | ORM 抽象层 |
| `db_is_running`..`db_poll_response` | 函数 | MongoDB | 数据库服务（5 个全局函数） |
| `bson` | table | MongoDB | BSON 文档操作 |
| `mongoc` | table | MongoDB | MongoDB 驱动 |
| `mem` | table | MEM_STATS | 内存统计 |
