# evpp 公共 API 参考 — Lua 绑定相关

## TCPClient 完整 API

```cpp
class TCPClient {
    // 构造
    TCPClient(EventLoop* loop, const std::string& remote_addr, const std::string& name);
    ~TCPClient();

    // 生命周期
    void Bind(const std::string& local_addr);
    void Connect();
    void Disconnect();

    // 回调设置
    void SetConnectionCallback(const ConnectionCallback& cb);
    void SetMessageCallback(const MessageCallback& cb);

    // 配置
    void set_auto_reconnect(bool v);          // 默认 true
    void set_reconnect_interval(Duration t);  // 默认 3s
    void set_connecting_timeout(Duration t);  // 默认 3s

    // 查询
    TCPConnPtr conn() const;
    const std::string& remote_addr() const;
    const std::string& name() const;
    EventLoop* loop() const;
};

// Callback: void(const TCPConnPtr&)  → 连接/断开时调用
// MessageCallback: void(const TCPConnPtr&, Buffer*) → 收到消息时调用
```

## TCPServer 完整 API

```cpp
class TCPServer {
    TCPServer(EventLoop* loop, const std::string& listen_addr,
              const std::string& name, uint32_t thread_num);
    bool Init();
    bool Start();
    void Stop(DoneCallback cb);
    void SetConnectionCallback(const ConnectionCallback& cb);
    void SetMessageCallback(MessageCallback cb);
    const std::string& listen_addr() const;
};
```

## TCPConn 完整 API

```cpp
class TCPConn {
    void Close();
    void Send(const char* s);
    void Send(const std::string& d);
    void Send(const Slice& message);
    void Send(Buffer* buf);

    // 查询
    EventLoop* loop() const;
    uint64_t id() const;
    const std::string& remote_addr() const;
    const std::string& local_addr() const;
    const std::string& name() const;
    bool IsConnected() const;
    bool IsDisconnected() const;

    // 配置
    void SetTCPNoDelay(bool on);
};
```

## Buffer 完整 API (接收数据相关)

```cpp
class Buffer {
    size_t length() const;         // 可读字节数
    const char* data() const;      // 可读数据起始指针
    std::string ToString() const;  // 全部可读数据转 string
    Slice ToSlice() const;         // 全部可读数据转 Slice
    std::string NextAllString();   // 取出全部数据（消费）
    Slice Next(size_t len);        // 取出 n 字节（消费）
    void Skip(size_t len);         // 跳过 n 字节
    void Reset();                   // 重置读写指针
};
```

## HTTP 客户端 API

```cpp
// 请求
class Request {
    Request(EventLoop* loop, const std::string& url, const std::string& body, Duration timeout);
    void Execute(const Handler& h);
    void AddHeader(const std::string& header, const std::string& value);
    void set_retry_number(int v);
};

class GetRequest : public Request {
    GetRequest(EventLoop* loop, const std::string& url, Duration timeout);
};

class PostRequest : public Request {
    PostRequest(EventLoop* loop, const std::string& url, const std::string& body, Duration timeout);
};

// 响应: Handler = function<void(shared_ptr<Response>)>
class Response {
    int http_code() const;
    const evpp::Slice& body() const;
    const char* FindHeader(const char* key);
};
```

## EventLoop 关键 API

```cpp
class EventLoop {
    void Run();     // 阻塞运行事件循环
    void Stop();    // 停止事件循环
    bool IsInLoopThread() const;

    InvokeTimerPtr RunAfter(double delay_ms, Functor&& f);
    InvokeTimerPtr RunEvery(Duration interval, Functor&& f);
    void RunInLoop(Functor&& handler);
};
```
