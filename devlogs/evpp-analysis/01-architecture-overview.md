# evpp 网络库架构分析

## 概述

evpp 是一个基于 libevent 的现代 C++ 事件驱动网络库，采用 Reactor 模式，提供 TCP/UDP/HTTP 服务端和客户端的完整封装。

**总计**: 80 个文件，约 12,100 行代码（46 头文件 + 31 实现文件 + 3 特殊文件）

## 分层架构

```
┌─────────────────────────────────────────────────────────┐
│  应用层: TCPServer, TCPClient, HTTP Server/Client, UDP  │
├─────────────────────────────────────────────────────────┤
│  连接层: TCPConn, Buffer, Connector, Listener            │
├─────────────────────────────────────────────────────────┤
│  事件层: EventLoop, FdChannel, EventWatcher, InvokeTimer │
├─────────────────────────────────────────────────────────┤
│  平台抽象: libevent.h, sys_sockets, windows_port         │
├─────────────────────────────────────────────────────────┤
│  libevent 1.4 / 2.x                                      │
└─────────────────────────────────────────────────────────┘
```

## 核心模块

### 1. EventLoop (事件驱动核心)

| 文件 | 行数 |
|------|------|
| event_loop.h | 136 |
| event_loop.cc | 365 |

- 封装 libevent 的 `event_base`
- **one loop per thread** 模型
- `Run()` / `Stop()` 控制生命周期
- `RunInLoop()` / `QueueInLoop()` 跨线程任务投递
- `RunAfter()` / `RunEvery()` 定时器接口
- 支持 Boost lockfree queue / cameron314 concurrentqueue 作为跨线程任务队列

### 2. TCP 连接 (TCPConn)

| 文件 | 行数 |
|------|------|
| tcp_conn.h | 192 |
| tcp_conn.cc | 324 |

状态机: `kDisconnected → kConnecting → kConnected → kDisconnecting → kDisconnected`

关键 API:
- `Send(const char* s)` / `Send(const std::string& d)` / `Send(Buffer* buf)` — 发送数据
- `Close()` — 关闭连接
- `IsConnected()` / `IsDisconnected()` / `IsDisconnecting()` — 状态查询
- `remote_addr()` / `local_addr()` — 地址查询
- `SetTCPNoDelay(bool)` — TCP_NODELAY 控制
- 内部 Buffer: input_buffer_ (读) + output_buffer_ (写)

### 3. TCP 服务端 (TCPServer)

| 文件 | 行数 |
|------|------|
| tcp_server.h | 122 |
| tcp_server.cc | 189 |
| listener.h | 44 |
| listener.cc | 97 |

多线程 TCP 服务器，使用 EventLoopThreadPool 进行连接分发。

关键 API:
```cpp
TCPServer(EventLoop* loop, const std::string& listen_addr,
          const std::string& name, uint32_t thread_num);
bool Init();
bool Start();
void Stop(DoneCallback cb);
void SetConnectionCallback(const ConnectionCallback& cb);
void SetMessageCallback(MessageCallback cb);
```

分发策略: ThreadDispatchPolicy (kRoundRobin / kIPAddressHashing)

### 4. TCP 客户端 (TCPClient)

| 文件 | 行数 |
|------|------|
| tcp_client.h | 128 |
| tcp_client.cc | 147 |
| connector.h | 68 |
| connector.cc | 257 |

异步 TCP 客户端，支持 DNS 解析、自动重连。

关键 API:
```cpp
TCPClient(EventLoop* loop, const std::string& remote_addr, const std::string& name);
void Bind(const std::string& local_addr);
void Connect();
void Disconnect();
void SetConnectionCallback(const ConnectionCallback& cb);
void SetMessageCallback(const MessageCallback& cb);
bool auto_reconnect() const;
void set_auto_reconnect(bool v);
```

### 5. Buffer (网络缓冲区)

| 文件 | 行数 |
|------|------|
| buffer.h | 435 |
| buffer.cc | 47 |

读写缓冲区，支持 prependable 空间设计。

关键 API:
- `Write(const void* d, size_t len)` — 追加数据
- `Next(size_t len)` — 读取 n 字节并推进读指针
- `NextAllString()` — 读取全部并转为 string
- `length()` / `size()` — 未读数据长度
- `ToSlice()` / `ToString()` — 查看数据
- `AppendInt64/32/16/8` — 网络字节序追加
- `ReadInt64/32/16/8` / `PeekInt64/32/16/8` — 网络字节序读取

### 6. HTTP 客户端 (httpc)

| 文件 | 行数 |
|------|------|
| httpc/request.h | 108 |
| httpc/request.cc | 243 |
| httpc/response.h | 48 |
| httpc/response.cc | 48 |
| httpc/conn.h | 70 |
| httpc/conn.cc | 115 |
| httpc/conn_pool.h | 57 |
| httpc/conn_pool.cc | 77 |

关键 API:
```cpp
// GET 请求
GetRequest(ConnPool* pool, EventLoop* loop, const std::string& url);
GetRequest(EventLoop* loop, const std::string& url, Duration timeout);

// POST 请求
PostRequest(ConnPool* pool, EventLoop* loop, const std::string& url, const std::string& body);
PostRequest(EventLoop* loop, const std::string& url, const std::string& body, Duration timeout);

void Execute(const Handler& h); // Handler = function<void(shared_ptr<Response>)>
void set_retry_number(int v);
void set_retry_interval(Duration d);
void AddHeader(const std::string& header, const std::string& value);

// Response
int http_code() const;
const evpp::Slice& body() const;
const char* FindHeader(const char* key);
```

### 7. 回调类型定义 (tcp_callbacks.h)

```cpp
typedef std::shared_ptr<TCPConn> TCPConnPtr;
typedef std::function<void(const TCPConnPtr&)> ConnectionCallback;
typedef std::function<void(const TCPConnPtr&)> CloseCallback;
typedef std::function<void(const TCPConnPtr&, Buffer*)> MessageCallback;
```

## 数据流

```
[网络数据] → libevent 触发读事件
    → FdChannel::HandleRead()
    → TCPConn::HandleRead()
    → input_buffer_.ReadFromFD(fd)
    → msg_fn_(conn, &input_buffer_)  // 用户回调
    → Buffer::Retrieve() / Skip() / Next() 消费数据

[发送数据]
    → TCPConn::Send(data)
    → output_buffer_.Write(data)
    → FdChannel 启用写事件
    → libevent 触发写事件 → HandleWrite()
    → ::send(fd, output_buffer_.data, ...)
    → Buffer::Retrieve() 清空已发送
```
