# CS Demo 使用教程

本文说明如何构建、启动并验证 Lua 实现的游戏客户端/服务器 CS demo。

## 目录

- 发布构建脚本：`scripts/build_release.py`
- 服务端入口：`resources/script/server/init.lua`
- 客户端入口：`resources/script/client/init.lua`
- 通信协议：`resources/script/shared/cs_protocol.lua`
- 客户端连接配置：`resources/config/client/client.json`
- 发布产物目录：`artifacts/release/Release/`

## 一键构建并验证

在工程根目录执行：

```powershell
python scripts\build_release.py --smoke
```

Windows 也可以直接使用 batch 包装脚本：

```bat
scripts\build_release.bat --smoke
```

该命令会完成三件事：

1. 构建 `GameServer`、`GameClient`、`GameClientApp`。
2. 打包发布产物到 `artifacts/release/Release/`。
3. 先启动 server，再启动 client，验证 Lua CS 通信闭环。

成功时终端会输出：

```text
[release] smoke passed: client connected to server and completed Lua CS round trip
```

## 手动启动

先构建发布产物：

```powershell
python scripts\build_release.py
```

或使用 batch 包装脚本：

```bat
scripts\build_release.bat
```

打开第一个 PowerShell，启动 server：

```powershell
cd artifacts\release\Release
.\run_server.bat
```

打开第二个 PowerShell，启动 client：

```powershell
cd artifacts\release\Release
.\run_client.bat
```

Linux/macOS 使用：

```sh
cd artifacts/release/Release
./run_server.sh
./run_client.sh
```

## 成功标志

server 日志应出现：

```text
CS_SERVER_LISTENING 127.0.0.1:7777
CS_SERVER_WELCOME player-2
```

client 日志应出现：

```text
CS_CLIENT_CONNECTED 127.0.0.1:7777
CS_CLIENT_SENT hello
CS_CLIENT_WELCOME session=2
CS_CLIENT_SUCCESS session=2
```

其中 `session=2` 可能随运行次数变化，判断成功以 `CS_CLIENT_SUCCESS` 为准。

## CS 通信流程

当前 demo 的通信行为全部由 Lua 实现，C++ 只负责宿主、事件循环和底层 TCP 绑定。

消息流程：

1. client 读取 `client.network.server_address` 和 `client.network.server_port`。
2. client 连接 server。
3. client 发送 `hello`。
4. server 创建会话并返回 `welcome`。
5. client 发送 `ping`、`input`、`chat`。
6. server 返回 `pong` 和 `authoritative_state`，并广播 `server_event`。
7. client 收到会话、pong 和权威状态后输出 `CS_CLIENT_SUCCESS`。

## 配置连接地址

默认配置位于：

```text
resources/config/client/client.json
```

关键字段：

```json
{
  "network": {
    "server_address": "127.0.0.1",
    "server_port": 7777,
    "reconnect_max_retries": 10,
    "reconnect_base_delay_ms": 500,
    "reconnect_max_delay_ms": 30000,
    "timeout_ms": 5000
  }
}
```

如果 server 部署在其他机器，把 `server_address` 改为服务器 IP；同时确认防火墙允许 TCP `server_port`。

## 修改协议或玩法

协议入口：

```text
resources/script/shared/cs_protocol.lua
```

服务端权威逻辑：

```text
resources/script/server/init.lua
```

客户端连接、重连、握手、输入上传逻辑：

```text
resources/script/client/init.lua
```

新增消息时建议按这个顺序改：

1. 在 client 中增加 `send("<op>", payload)`。
2. 在 server 的 `handlers` 表里注册 `<op>` 处理函数。
3. server 用 `send(conn, "<reply_op>", payload, seq)` 返回结果。
4. 在 client 的 `handle_message()` 中处理 `<reply_op>`。

## 常见问题

`server did not listen on 127.0.0.1:7777`

端口被占用或 server 启动失败。换端口后同步修改 `resources/config/client/client.json`。

`CS_CLIENT_FAILED timeout`

client 已启动但没有完成协议闭环。检查 server 是否正在运行、地址端口是否一致、server 日志是否出现 `CS_SERVER_LISTENING`。

client 连接不上远程 server

确认 `server_address` 使用的是 server 对 client 可达的 IP，且防火墙放行 TCP 端口。

看不到 `CS_CLIENT_SUCCESS`

先用 `python scripts\build_release.py --smoke` 验证本机闭环，再检查手动启动时是否先启动 server 后启动 client。

## 重新生成产物

发布产物是可再生成内容，默认不提交到 Git：

```powershell
python scripts\build_release.py --smoke
```

Windows 也可以执行：

```bat
scripts\build_release.bat --smoke
```

需要清理后重新打包时，直接重跑脚本即可；脚本默认会重建 `artifacts/release/Release/`。
