# GameClient — Pure C API for the game Runtime

**GameClient** 是一个纯 C 接口的动态库，将 CloudEngine 的全部核心功能（Lua 虚拟机、网络、定时器、日志）封装为 ABI 稳定的 C 函数，供 Unity、Unreal Engine 等游戏引擎通过 `[DllImport]` / `dlsym` 直接调用。

## 设计目标

游戏服务器和客户端使用**统一的技术栈**实现业务逻辑：

```
┌──────────────────────────────────────────────────┐
│                  游戏业务逻辑                       │
│            (Lua — 服务器 & 客户端共享)               │
├──────────────────────┬───────────────────────────┤
│     CloudEngine     │      GameClient            │
│   (源码内嵌运行时)     │   (纯 C API 动态库)          │
│   • 完整功能          │   • extern "C" 导出         │
│   • Lua bindings    │   • 不透明句柄               │
│   • libevent        │   • ABI 稳定                │
├──────────────────────┼───────────────────────────┤
│   游戏服务器进程        │   游戏客户端进程              │
│   (Linux/Windows)    │   (Unity / UE / 自研引擎)    │
└──────────────────────┴───────────────────────────┘
```

- **服务器侧**：`GameServer` 直接以内嵌源码方式集成 runtime，拥有全部功能（物理、Lua 热更、完整配置）。
- **客户端侧**：使用 `GameClient.dll`，通过纯 C API 获取核心功能（Lua VM、网络、定时器、日志），可集成到任何引擎。

客户端和服务器可以共享完全相同的 Lua 业务代码——网络协议处理、战斗逻辑校验、数据校验规则等。

## 目录结构

```
src/client/
├── README.md
├── CMakeLists.txt
├── include/
│   └── client.h                 # 唯一公共头文件（C 和 C++ 均可包含）
├── src/
│   ├── client.cpp               # 引擎生命周期、Lua 脚本、工具函数
│   ├── client_internal.h        # 内部类型定义（不对外暴露）
│   ├── client_log.cpp           # 日志封装
│   ├── client_timer.cpp         # 定时器封装
│   ├── client_net.cpp           # TCP 客户端/服务器 + HTTP 客户端
│   ├── client_udp.cpp           # UDP 客户端/服务器
│   └── client_kcp.cpp           # KCP 客户端/服务器
└── examples/
    └── basic_usage.c            # 完整示例
```

## 快速开始

### 1. 构建

```bash
cd src/client
mkdir build && cd build
cmake .. -DGAME_SRC_DIR=/path/to/src -DGAME_ARTIFACTS_DIR=/path/to/artifacts
cmake --build .
```

生成产物：
- **Windows**: `GameClient.dll` + `GameClient.lib`
- **Linux**: `libGameClient.so`

### 2. 最小示例

```c
#include "client.h"

int main() {
    game_client_t* cli;
    game_client_create(&cli);
    game_client_init(cli, NULL);   // NULL = 默认配置

    // 执行 Lua 脚本
    game_client_do_string(cli, "print('hello from Lua!')", NULL, 0);

    // 游戏主循环
    for (int frame = 0; frame < 1000; frame++) {
        game_client_tick(cli);     // 驱动事件循环
    }

    game_client_destroy(&cli);
    return 0;
}
```

### 3. 完整示例

参见 `examples/basic_usage.c` — 包含引擎初始化、Lua 脚本执行、定时器、HTTP 请求的完整示例。

---

## API 参考

### 约定

| 约定 | 说明 |
|------|------|
| 返回值 | 大部分函数返回 `game_error_t`。`GAME_OK`(0) 表示成功，负值表示错误 |
| 错误信息 | 多数函数在失败时设置引擎的内部错误字符串，可通过 `game_client_last_error()` 获取 |
| 字符串 | 所有字符串均为 UTF-8，`data_len` 参数允许嵌入 `\0` |
| 内存所有权 | 调用者通过 `_create`/`_connect`/`_listen` 获得句柄所有权，通过 `_destroy`/`_close`/`_stop`/`_disconnect` 释放 |
| 回调中的数据 | 仅在回调期间有效，如需持久保存请立即复制 |
| 线程 | 所有函数必须在同一线程调用（驱动事件循环的线程） |

### 错误码

| 错误码 | 值 | 含义 |
|--------|-----|------|
| `GAME_OK` | 0 | 成功 |
| `GAME_ERR_GENERIC` | -1 | 未指定错误 |
| `GAME_ERR_INVALID_ARG` | -2 | 参数无效（NULL 或越界） |
| `GAME_ERR_NOT_FOUND` | -3 | 资源未找到 |
| `GAME_ERR_NOT_CONNECTED` | -4 | 网络操作在已关闭的连接上执行 |
| `GAME_ERR_TIMEOUT` | -5 | 操作超时 |
| `GAME_ERR_SCRIPT` | -6 | Lua 脚本编译/运行错误 |
| `GAME_ERR_NETWORK` | -7 | 网络层故障（连接失败、DNS 解析错误） |
| `GAME_ERR_ALREADY_EXISTS` | -8 | 资源已创建/已连接 |
| `GAME_ERR_OUT_OF_MEMORY` | -9 | 内存分配失败 |

---

### 引擎生命周期

```c
game_error_t game_client_create(game_client_t** out_client);
game_error_t game_client_init(game_client_t* client, const char* config_dir);
game_error_t game_client_tick(game_client_t* client);
game_error_t game_client_run(game_client_t* client);     // 阻塞模式
game_error_t game_client_stop(game_client_t* client);    // 线程安全
void         game_client_destroy(game_client_t** client);
bool         game_client_is_running(game_client_t* client);
```

#### 两种运行模式

**Tick 模式（推荐用于游戏引擎）**

游戏引擎每帧调用 `game_client_tick()` 驱动事件循环：

```c
game_client_init(cli, NULL);   // library mode: 引擎不启动自己的帧定时器

while (game_running) {
    float dt = get_delta_time();
    update_game(dt);
    game_client_tick(cli);     // 处理网络事件、定时器回调、Lua UpdateScript()
    render();
}

game_client_destroy(&cli);
```

**Run 模式（独立运行）**

`game_client_run()` 启动引擎自己的事件循环，阻塞直到 `game_client_stop()` 被调用。

```c
// 在另一个线程或信号处理函数中调用
game_client_stop(cli);
```

#### 注意事项

- 进程内只能有一个 `game_client_t` 实例（底层 `Engine` 是单例）。
- `game_client_init` 会自动加载配置（如果 `config_dir` 不为 NULL）并初始化 Lua VM。
- `game_client_destroy` 会释放所有网络句柄——在此之前不需要单独关闭每个连接。

---

### Lua 脚本

```c
game_error_t game_client_do_string(game_client_t* client,
                                   const char* script,
                                   char* error_out, int error_size);

game_error_t game_client_do_file(game_client_t* client,
                                 const char* filename,
                                 char* error_out, int error_size);

game_error_t game_client_register_function(game_client_t* client,
                                           const char* name,
                                           game_lua_cfunction_t func);

void* game_client_get_lua_state(game_client_t* client);
```

#### 注册 C 函数到 Lua

```c
int my_native_func(void* L_ptr) {
    lua_State* L = (lua_State*)L_ptr;
    const char* input = luaL_checkstring(L, 1);
    lua_pushfstring(L, "C says: %s", input);
    return 1;  // 返回值的数量
}

game_client_register_function(cli, "native_greet", my_native_func);

// Lua 中调用:
// local result = native_greet("world")  -- result = "C says: world"
```

#### 获取 lua_State 指针

```c
lua_State* L = (lua_State*)game_client_get_lua_state(cli);
// 可以直接使用 Lua C API 进行高级操作
// 注意：需要自行管理栈平衡
```

---

### 日志

```c
void game_log_trace(game_client_t* client, const char* msg);
void game_log_debug(game_client_t* client, const char* msg);
void game_log_info (game_client_t* client, const char* msg);
void game_log_warn (game_client_t* client, const char* msg);
void game_log_error(game_client_t* client, const char* msg);
void game_log_fatal(game_client_t* client, const char* msg);
```

日志通过 Quill 异步日志库输出。线程安全。日志自动带 `[capi]` 前缀以区别于引擎内部日志。

---

### 定时器

```c
game_error_t game_timer_timeout(game_client_t* client,
                                int64_t delay_ms,
                                game_timer_cb_t cb, void* userdata,
                                int* out_id);

game_error_t game_timer_interval(game_client_t* client,
                                 int64_t interval_ms,
                                 game_timer_cb_t cb, void* userdata,
                                 int* out_id);

game_error_t game_timer_cancel(game_client_t* client, int timer_id);
```

#### 回调签名

```c
typedef void (*game_timer_cb_t)(int timer_id, void* userdata);
```

#### 示例

```c
void on_tick(int timer_id, void* ud) {
    int* counter = (int*)ud;
    (*counter)++;
    printf("tick %d\n", *counter);
}

int counter = 0;
int timer_id;
game_timer_interval(cli, 1000, on_tick, &counter, &timer_id);

// 之后取消
game_timer_cancel(cli, timer_id);  // 可在回调内部安全调用
```

- `game_timer_timeout`: 一次性定时器，触发后自动销毁。
- `game_timer_interval`: 周期性定时器，每 `interval_ms` 毫秒触发一次，直到被取消。
- 回调在事件循环线程（即调用 `game_client_tick()` 的线程）上执行。

---

### TCP 客户端

```c
game_error_t game_tcp_connect(game_client_t* client, const char* addr,
                              game_net_client_t** out_conn);
game_error_t game_tcp_send(game_net_client_t* conn, const char* data, int data_len);
void         game_tcp_disconnect(game_net_client_t** conn);
bool         game_tcp_is_connected(game_net_client_t* conn);

void game_tcp_set_on_connect(game_net_client_t* conn, game_tcp_connect_cb_t cb, void* ud);
void game_tcp_set_on_message(game_net_client_t* conn, game_tcp_message_cb_t cb, void* ud);
void game_tcp_set_on_close  (game_net_client_t* conn, game_tcp_close_cb_t   cb, void* ud);
```

#### 回调签名

```c
typedef void (*game_tcp_connect_cb_t)(game_net_client_t* cli, void* userdata);
typedef void (*game_tcp_message_cb_t)(game_net_client_t* cli,
                                      const char* data, int data_len, void* userdata);
typedef void (*game_tcp_close_cb_t)  (game_net_client_t* cli, void* userdata);
```

#### 示例

```c
game_net_client_t* conn;
game_tcp_connect(cli, "127.0.0.1:8080", &conn);

game_tcp_set_on_connect(conn, on_connect, NULL);
game_tcp_set_on_message(conn, on_message, NULL);
game_tcp_set_on_close(conn, on_close, NULL);

// 在 on_connect 中发送数据
void on_connect(game_net_client_t* c, void* ud) {
    game_tcp_send(c, "hello", 5);
}
```

#### 注意事项

- `connect` 是异步的：函数立即返回，连接建立成功后会触发 `on_connect` 回调。
- `on_close` 仅在**被动断开**时触发（对端关闭、网络异常），手动调用 `game_tcp_disconnect` 不触发。
- `game_tcp_disconnect` 同时释放句柄，调用后 `conn` 被置为 NULL。

---

### TCP 服务器

```c
game_error_t game_tcp_listen(game_client_t* client, const char* addr,
                             game_net_server_t** out_server);
void game_tcp_server_stop(game_net_server_t** server);

void game_tcp_server_set_on_connect(game_net_server_t* srv, game_tcp_server_connect_cb_t cb, void* ud);
void game_tcp_server_set_on_message(game_net_server_t* srv, game_tcp_server_message_cb_t cb, void* ud);
void game_tcp_server_set_on_close  (game_net_server_t* srv, game_tcp_server_close_cb_t   cb, void* ud);
```

#### 回调签名

```c
typedef void (*game_tcp_server_connect_cb_t)(game_tcp_conn_t* conn,
                                             const char* remote_addr, void* userdata);
typedef void (*game_tcp_server_message_cb_t)(game_tcp_conn_t* conn,
                                             const char* data, int data_len, void* userdata);
typedef void (*game_tcp_server_close_cb_t)  (game_tcp_conn_t* conn,
                                             const char* remote_addr, void* userdata);
```

#### 连接操作

```c
game_error_t game_tcp_conn_send(game_tcp_conn_t* conn, const char* data, int data_len);
void         game_tcp_conn_close(game_tcp_conn_t** conn);
bool         game_tcp_conn_is_connected(game_tcp_conn_t* conn);

void game_tcp_conn_set_on_message(game_net_server_t*, game_tcp_conn_t* conn,
                                  game_tcp_conn_message_cb_t cb, void* ud);
void game_tcp_conn_set_on_close  (game_net_server_t*, game_tcp_conn_t* conn,
                                  game_tcp_conn_close_cb_t   cb, void* ud);
```

#### 示例

```c
void on_new_conn(game_tcp_conn_t* conn, const char* remote_addr, void* ud) {
    printf("new connection from %s\n", remote_addr);
    game_tcp_conn_set_on_message(NULL, conn, on_conn_msg, NULL);
}

void on_conn_msg(game_tcp_conn_t* conn, const char* data, int len, void* ud) {
    game_tcp_conn_send(conn, data, len);  // echo
}

game_net_server_t* server;
game_tcp_listen(cli, "0.0.0.0:8080", &server);
game_tcp_server_set_on_connect(server, on_new_conn, NULL);
```

#### 回调优先级

连接级别的 `on_message` / `on_close` 优先级高于服务器级别的默认回调。如果连接上有自己的回调，服务器级回调不会被调用。

---

### HTTP 客户端

```c
game_error_t game_http_get (game_client_t* client, const char* url,
                            game_http_response_cb_t cb, void* userdata);
game_error_t game_http_post(game_client_t* client, const char* url,
                            const char* body, int body_len,
                            game_http_response_cb_t cb, void* userdata);
```

#### 回调签名

```c
typedef void (*game_http_response_cb_t)(int http_code,
                                        const char* body, int body_len,
                                        void* userdata);
```

- 请求为异步，在后台线程执行，完成后在主线程触发回调。
- `http_code` 为 0 时表示请求失败或超时（超时由 `server.json` 中 `http.timeout_sec` 控制）。
- `body` 和 `body_len` 在超时/失败时为 NULL 和 0。

#### 示例

```c
void on_response(int code, const char* body, int body_len, void* ud) {
    printf("HTTP %d: %.*s\n", code, body_len, body);
}

game_http_get(cli, "https://api.example.com/status", on_response, NULL);

const char* json = "{\"key\":\"value\"}";
game_http_post(cli, "https://api.example.com/submit", json, strlen(json),
               on_response, NULL);
```

---

### UDP 客户端

```c
game_error_t game_udp_connect(game_client_t* client, const char* host, int port,
                              game_udp_client_t** out_udp);
game_error_t game_udp_send    (game_udp_client_t* udp, const char* data, int data_len);
game_error_t game_udp_request (game_udp_client_t* udp, const char* data, int data_len,
                               int timeout_ms, char* resp_buf, int resp_cap,
                               int* out_resp_len);
game_error_t game_udp_request_to(game_client_t* client, const char* host, int port,
                                 const char* data, int data_len, int timeout_ms,
                                 char* resp_buf, int resp_cap, int* out_resp_len);
game_error_t game_udp_send_to(game_client_t* client, const char* host, int port,
                              const char* data, int data_len);
void         game_udp_close(game_udp_client_t** udp);
bool         game_udp_is_connected(game_udp_client_t* udp);
```

#### 使用模式

**实例模式**（建立长连接，多次 send/request）：
```c
game_udp_client_t* udp;
game_udp_connect(cli, "127.0.0.1", 9000, &udp);
game_udp_send(udp, "ping", 4);
char buf[256]; int resp_len;
game_udp_request(udp, "query", 5, 3000, buf, sizeof(buf), &resp_len);
game_udp_close(&udp);
```

**快捷方法**（一次性操作，自动建立和销毁临时连接）：
```c
// 单向发送（fire-and-forget）
game_udp_send_to(cli, "127.0.0.1", 9000, "log data", 8);

// 请求-响应
char buf[256]; int resp_len;
game_udp_request_to(cli, "127.0.0.1", 9000, "query", 5, 3000,
                    buf, sizeof(buf), &resp_len);
```

> UDP 操作是**同步阻塞**的。`send_to` 和 `do_request` 会阻塞当前线程。

---

### UDP 服务器

```c
game_error_t game_udp_listen(game_client_t* client, int port,
                             game_udp_message_cb_t cb, void* userdata,
                             game_udp_server_t** out_udp);
void game_udp_server_stop      (game_udp_server_t** server);
void game_udp_server_pause     (game_udp_server_t* server);
void game_udp_server_resume    (game_udp_server_t* server);
bool game_udp_server_is_running(game_udp_server_t* server);
void game_udp_server_set_on_message(game_udp_server_t* server,
                                    game_udp_message_cb_t cb, void* userdata);
```

#### 回调签名

```c
typedef void (*game_udp_message_cb_t)(const char* data, int data_len,
                                      const char* remote_ip, void* userdata);
```

#### 示例

```c
void on_udp(const char* data, int len, const char* from_ip, void* ud) {
    printf("UDP from %s: %.*s\n", from_ip, len, data);
}

game_udp_server_t* udp_srv;
game_udp_listen(cli, 5353, on_udp, NULL, &udp_srv);
// ...
game_udp_server_stop(&udp_srv);
```

- 回调在事件循环主线程触发（线程安全）。
- 通过 `pause()` / `resume()` 可以暂停和恢复接收。
- `set_on_message()` 原子性地替换回调，旧回调会在投递中的消息处理完成后安全释放。

---

### KCP 客户端（可靠 UDP）

```c
game_error_t game_kcp_connect(game_client_t* client, const char* host, int port,
                              uint32_t conv, game_kcp_client_t** out_kcp);
game_error_t game_kcp_new   (uint32_t conv, game_kcp_client_t** out_kcp);
game_error_t game_kcp_client_connect(game_kcp_client_t* kcp, const char* host, int port);
game_error_t game_kcp_send   (game_kcp_client_t* kcp, const char* data, int data_len);
game_error_t game_kcp_request(game_kcp_client_t* kcp, const char* data, int data_len,
                              int timeout_ms, char* resp_buf, int resp_cap,
                              int* out_resp_len);
void         game_kcp_close(game_kcp_client_t** kcp);
bool         game_kcp_is_connected(game_kcp_client_t* kcp);

// KCP 参数调优（在 connect 之前调用）
void game_kcp_set_nodelay  (game_kcp_client_t* kcp, int nodelay, int interval, int resend, int nc);
void game_kcp_set_wnd_size (game_kcp_client_t* kcp, int sndwnd, int rcvwnd);
void game_kcp_set_mtu      (game_kcp_client_t* kcp, int mtu);
void game_kcp_set_conv     (game_kcp_client_t* kcp, uint32_t conv);
```

#### KCP 参数说明

KCP 是一个快速可靠的 ARQ 协议，运行在 UDP 之上，适合对延迟敏感的场景。

| 参数 | 含义 | 典型值 |
|------|------|--------|
| `nodelay` | 0=禁用, 1=启用 nodelay, 2=启用+流速控制 | 1 (快速模式) |
| `interval` | 内部时钟间隔（毫秒） | 10 |
| `resend` | 快速重传阈值（0=禁用, 2=常见） | 2 |
| `nc` | 0=正常拥塞控制, 1=禁用（全速） | 1 |
| `sndwnd` | 发送窗口（包数） | 128 |
| `rcvwnd` | 接收窗口（包数） | 128 |
| `mtu` | 最大传输单元（字节） | 1400 |
| `conv` | 会话 ID（双方必须一致） | 自定义 |

**推荐配置**：

```c
// 快速模式（游戏实时通信）
game_kcp_set_nodelay(kcp, 1, 10, 2, 1);
game_kcp_set_wnd_size(kcp, 128, 128);
game_kcp_set_mtu(kcp, 1400);

// 普通模式（可靠传输优先）
game_kcp_set_nodelay(kcp, 0, 40, 0, 0);
game_kcp_set_wnd_size(kcp, 128, 128);
```

#### 使用模式

**一步连接**（默认参数）：
```c
game_kcp_client_t* kcp;
game_kcp_connect(cli, "127.0.0.1", 9000, 12345, &kcp);
game_kcp_send(kcp, "hello", 5);
```

**两步创建**（先调优后连接）：
```c
game_kcp_client_t* kcp;
game_kcp_new(12345, &kcp);
game_kcp_set_nodelay(kcp, 1, 10, 2, 1);
game_kcp_client_connect(kcp, "127.0.0.1", 9000);
game_kcp_send(kcp, "hello", 5);
```

> KCP 客户端操作是**同步阻塞**的。

---

### KCP 服务器

```c
game_error_t game_kcp_listen(game_client_t* client, int port,
                             game_kcp_message_cb_t cb, void* userdata,
                             game_kcp_server_t** out_kcp);
void game_kcp_server_stop       (game_kcp_server_t** server);
void game_kcp_server_pause      (game_kcp_server_t* server);
void game_kcp_server_resume     (game_kcp_server_t* server);
bool game_kcp_server_is_running (game_kcp_server_t* server);
void game_kcp_server_set_on_message(game_kcp_server_t* server,
                                    game_kcp_message_cb_t cb, void* userdata);

// 参数调优
void game_kcp_server_set_nodelay        (game_kcp_server_t*, int nodelay, int interval, int resend, int nc);
void game_kcp_server_set_wnd_size       (game_kcp_server_t*, int sndwnd, int rcvwnd);
void game_kcp_server_set_mtu            (game_kcp_server_t*, int mtu);
void game_kcp_server_set_session_timeout(game_kcp_server_t*, int timeout_ms);
```

#### 回调签名

```c
typedef void (*game_kcp_message_cb_t)(const char* data, int data_len,
                                      const char* remote_ip, uint32_t conv,
                                      void* userdata);
```

- `conv` 参数标识了消息来自哪个 KCP 会话，服务器端可以据此区分不同客户端。
- `set_session_timeout()` 设置会话超时时间（毫秒），超时的会话会自动清理。

---

### 工具函数

```c
const char* game_version(void);
int  game_client_last_error(game_client_t* client, char* buf, int buf_size);
void* game_client_get_lua_state(game_client_t* client);
```

---

## 引擎集成指南

### Unity 集成

1. 将 `GameClient.dll`（Windows）或 `libGameClient.so`（Linux）放入 `Assets/Plugins/`。

2. 创建 C# 绑定文件 `GameClient.cs`：

```csharp
using System;
using System.Runtime.InteropServices;

public enum GameError {
    OK = 0,
    Generic = -1,
    InvalidArg = -2,
    NotFound = -3,
    NotConnected = -4,
    Timeout = -5,
    Script = -6,
    Network = -7,
    AlreadyExists = -8,
    OutOfMemory = -9,
}

public static class GameClient
{
    const string DllName = "GameClient";

    [DllImport(DllName)] public static extern IntPtr game_version();
    [DllImport(DllName)] public static extern GameError game_client_create(out IntPtr client);
    [DllImport(DllName)] public static extern GameError game_client_init(IntPtr client, string configDir);
    [DllImport(DllName)] public static extern GameError game_client_tick(IntPtr client);
    [DllImport(DllName)] public static extern void      game_client_destroy(ref IntPtr client);
    [DllImport(DllName)] public static extern bool      game_client_is_running(IntPtr client);
    [DllImport(DllName)] public static extern GameError game_client_do_string(IntPtr client, string script, IntPtr errOut, int errSize);

    // 日志
    [DllImport(DllName)] public static extern void game_log_info(IntPtr client, string msg);
    [DllImport(DllName)] public static extern void game_log_error(IntPtr client, string msg);

    // HTTP
    [DllImport(DllName)] public static extern GameError game_http_get(IntPtr client, string url, GameHttpCallback cb, IntPtr userdata);

    // ... 其他函数按相同模式绑定

    public delegate void GameHttpCallback(int httpCode, IntPtr body, int bodyLen, IntPtr userdata);
}
```

3. 在 Unity MonoBehaviour 中使用：

```csharp
public class GameNetwork : MonoBehaviour
{
    private IntPtr _client;

    void Awake()
    {
        GameClient.game_client_create(out _client);
        GameClient.game_client_init(_client, null);
        GameClient.game_client_do_string(_client, "print('Unity says hello!')", IntPtr.Zero, 0);
    }

    void Update()
    {
        GameClient.game_client_tick(_client);
    }

    void OnDestroy()
    {
        GameClient.game_client_destroy(ref _client);
    }
}
```

### Unreal Engine 集成

1. 将 DLL 和头文件放入 `Source/ThirdParty/GameClient/`。

2. 创建 `GameClient.Build.cs`：

```csharp
public class GameClient : ModuleRules
{
    public GameClient(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicIncludePaths.Add(ModuleDirectory);
        PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "GameClient.lib"));
        RuntimeDependencies.Add("$(BinaryOutputDir)/GameClient.dll",
            Path.Combine(ModuleDirectory, "GameClient.dll"));
    }
}
```

3. 在 C++ 游戏代码中使用：

```cpp
#include "client.h"

void UMyGameInstance::Init()
{
    game_client_t* Client;
    game_client_create(&Client);
    game_client_init(Client, nullptr);

    // 注册 tick 到引擎的 update 委托
    FCoreDelegates::OnBeginFrame.AddLambda([this, Client]() {
        game_client_tick(Client);
    });
}

void UMyGameInstance::Shutdown()
{
    game_client_destroy(&Client);
}
```

### 自定义 C/C++ 引擎集成

直接将 `game_client.h` 复制到项目中，链接动态库即可：

```cmake
target_link_libraries(my_game GameClient)
```

```c
#include "client.h"
// 直接使用所有 API
```

---

## 线程模型

```
┌──────────────────────────────────────────────┐
│                Game Thread                    │
│                                              │
│  game_client_tick()   ← 每帧调用              │
│       │                                      │
│       ├─ 处理 libevent 事件                    │
│       ├─ 触发 TCP/HTTP 回调                   │
│       ├─ 触发定时器回调                         │
│       ├─ 发送 UDP/KCP 数据（同步）              │
│       ├─ 调用 Lua UpdateScript()              │
│       └─ 返回                                 │
│                                              │
│  所有回调在此线程触发！                          │
└──────────────────────────────────────────────┘

┌──────────────────────────────────────────────┐
│           Background Threads                  │
│                                              │
│  • HTTP 请求/响应处理                          │
│  • UDP/KCP 服务端 recv 线程                    │
│  • Quill 日志 I/O                             │
│                                              │
│  这些线程不直接调用用户回调。回调通过             │
│  event loop 调度到 Game Thread。               │
└──────────────────────────────────────────────┘
```

**关键规则**：
- 所有 client API 函数必须从**同一个线程**调用（Game Thread）。
- 所有回调都在调用 `game_client_tick()` 的线程上执行。
- `game_client_stop()` 是唯一可从其他线程安全调用的函数。

---

## 内存管理

### 句柄生命周期

```
game_client_create() ──────────► game_client_destroy()
       │
       ├─ game_tcp_connect() ───► game_tcp_disconnect()
       ├─ game_tcp_listen() ────► game_tcp_server_stop()
       ├─ game_udp_connect() ───► game_udp_close()
       ├─ game_udp_listen() ────► game_udp_server_stop()
       ├─ game_kcp_connect() ───► game_kcp_close()
       └─ game_kcp_listen() ────► game_kcp_server_stop()
```

- 每个 `Create/Connect/Listen` 都对应一个 `Destroy/Close/Stop`。
- `game_client_destroy()` 会级联释放所有未关闭的网络句柄。
- `destroy` 函数接受 `T**` 并在成功后将其置为 NULL，防止悬空指针。

### 回调中的数据

```c
void on_message(game_net_client_t* conn, const char* data, int len, void* ud)
{
    // data 仅在回调期间有效！如需持久保存，复制它：
    char* my_copy = malloc(len);
    memcpy(my_copy, data, len);
    // ... 稍后释放 my_copy
}
```

---

## 与 CloudEngine 的关系

`client` 是 `CloudEngine` 的瘦封装层：

```
GameClient.dll ──(embed)─► runtime sources
                              │
                              ├─ engine::Engine (单例)
                              ├─ engine::ScriptVM (Lua VM)
                              ├─ 所有 Lua 绑定 (net, timer, log, ...)
                              ├─ evpp 网络库 (libevent)
                              └─ Quill 日志库
```

**`client` 不做的事情**：
- 不重新实现任何网络或脚本逻辑
- 不引入新的线程模型
- 不修改 Lua bindings 的行为

**`client` 做的事情**：
- 将 C++ 类和 Lua 绑定桥接为 ABI 稳定的 C 函数
- 提供不透明句柄，隐藏 C++ 类型
- 管理 C 回调到 Lua 回调的桥接
- 提供严格的输入验证和错误报告

---

## 构建要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | ≥ 3.20 | 构建系统 |
| C++17 编译器 | MSVC 2019+, GCC 9+, Clang 10+ | 实现文件为 C++ |
| CloudEngine | 当前版本 | 运行时依赖 |
| Lua 5.4 | 随 CloudEngine 提供 | 头文件路径 |

## 许可证

与 CloudEngine 项目相同。
