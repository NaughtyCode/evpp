# Lua 网络绑定设计方案

## 设计目标

将 evpp 的 TCP Client / Server / HTTP Client 封装导出到 Lua，使脚本层能：
1. 作为 TCP 客户端连接到远程服务器，收发消息
2. 作为 TCP 服务端监听端口，接受/管理连接，收发消息
3. 发起 HTTP GET/POST 请求，处理响应

## 模块划分

```
net.client   — TCP 异步客户端
net.server   — TCP 多线程服务端  
net.http     — HTTP 异步客户端
```

## 关键设计决策

1. **共享 EventLoop**: 所有网络对象复用 Engine 的 EventLoop，避免多线程复杂性
2. **回调映射**: Lua 函数引用存储在 C++ map 中，异步触发时通过 ref 回调 Lua
3. **ID 管理**: client_id / server_id / conn_id 用于 Lua 层标识各对象
4. **生命周期**: 通过 shared_ptr + 回调析构确保 C++ 对象在 Lua 引用释放后正确清理

## API 合约

### net.client

```lua
-- 连接服务器，返回 client_id (失败返回 nil, errmsg)
local cid = net.client.connect("127.0.0.1:8080",
    function()                          -- on_connect
        print("connected")
    end,
    function(data)                      -- on_message(data: string)
        print("received: " .. data)
    end,
    function()                          -- on_close
        print("disconnected")
    end)

-- 发送数据
net.client.send(cid, "hello world")

-- 断开连接
net.client.disconnect(cid)

-- 查询状态
local ok = net.client.is_connected(cid)
```

### net.server

```lua
-- 开始监听，返回 server_id
local sid = net.server.listen("0.0.0.0:9099",
    function(conn_id, remote_addr)      -- on_connect
        print("new conn " .. conn_id .. " from " .. remote_addr)
    end,
    function(conn_id, data)             -- on_message
        print("msg from " .. conn_id .. ": " .. data)
        -- echo back
        net.server.send(conn_id, data)
    end,
    function(conn_id)                   -- on_close
        print("conn " .. conn_id .. " closed")
    end)

-- 向指定连接发送数据
net.server.send(conn_id, "hello client")

-- 关闭指定连接
net.server.close_conn(conn_id)

-- 停止服务器
net.server.stop(sid)
```

### net.http

```lua
-- GET 请求
net.http.get("http://example.com/api/data",
    function(code, body)                -- on_response
        print("HTTP " .. code .. ": " .. body)
    end)

-- POST 请求  
net.http.post("http://example.com/api/data", "post body",
    function(code, body)
        print("HTTP " .. code .. ": " .. body)
    end)

-- 带超时的请求
net.http.get_timeout("http://example.com/api", 5000,
    function(code, body) end)
```

## 内存管理

- C++ 对象 (TCPClient/TCPServer) 存储在 engine 侧的 map 中
- Lua 回调通过 luaL_ref 存储在 registry 中
- 连接关闭时自动 unref Lua 回调
- Shutdown 时批量清理所有残余对象和回调引用
