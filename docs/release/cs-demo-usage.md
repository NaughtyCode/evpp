# CS Demo 使用教程

本文说明如何构建、启动并验证独立的 Lua 客户端/服务器 CS demo。

## 源码位置

CS demo 不再放在基础工程脚本目录中。源码独立位于：

- `resources/demos/cs/script/client/init.lua`
- `resources/demos/cs/script/server/init.lua`
- `resources/demos/cs/script/shared/cs_protocol.lua`
- `resources/demos/cs/config/client/client.json`
- `resources/demos/cs/config/server/server.json`

基础运行时脚本仍位于 `resources/script/runtime/`。发布脚本会把 demo 作为 overlay 打包到发布产物：

- `artifacts/release/Release/resources/demos/cs/script/client/init.lua`
- `artifacts/release/Release/resources/demos/cs/script/server/init.lua`
- `artifacts/release/Release/resources/demos/cs/script/shared/cs_protocol.lua`

## 一键构建并验证

在工程根目录执行：

```powershell
python scripts\build_release.py --smoke
```

Windows 也可以使用 batch 包装脚本：

```bat
scripts\build_release.bat --smoke
```

也可以直接使用 CS demo 专用发布脚本。该脚本默认开启 smoke 验证：

```bat
scripts\build_cs_demo_release.bat
```

该命令会：

1. 构建 `GameServer`、`GameClient`、`GameClientApp`。
2. 打包发布产物到 `artifacts/release/Release/`。
3. 把 `resources/demos/cs` overlay 写入发布产物。
4. 先启动 server，再启动 client，验证 Lua CS 通信闭环。

成功时终端会输出：

```text
[release] smoke passed: client connected to server and completed Lua CS round trip
```

如果只想打基础发布包，不包含 CS demo overlay，可以执行：

```powershell
python scripts\build_release.py --no-cs-demo
```

`--smoke` 需要 CS demo overlay，因此不能和 `--no-cs-demo` 一起使用。

## 手动启动

先生成发布产物：

```powershell
python scripts\build_release.py
```

或使用 CS demo 专用 batch 脚本生成并验证：

```bat
scripts\build_cs_demo_release.bat
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

需要自定义日志文件前缀时，把 `--log_prefix=<name>` 传给启动脚本：

```bat
.\run_server.bat --log_prefix=ShardA
.\run_client.bat --log_prefix=ClientA
```

未传 `--log_prefix` 时，默认前缀与当前程序名一致，例如 `GameServer`、`GameClientApp`。

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

`session=2` 可能随运行次数变化，判断成功以 `CS_CLIENT_SUCCESS` 为准。

## CS 通信流程

demo 的通信行为全部由 Lua 实现，C++ 只负责宿主、事件循环和底层 TCP 绑定。

1. client 读取 `client.network.server_address` 和 `client.network.server_port`。
2. client 连接 server。
3. client 发送 `hello`。
4. server 创建会话并返回 `welcome`。
5. client 发送 `ping`、`input`、`chat`。
6. server 返回 `pong` 和 `authoritative_state`，并广播 `server_event`。
7. client 收到会话、pong 和权威状态后输出 `CS_CLIENT_SUCCESS`。

## 修改协议或玩法

协议入口：

```text
resources/demos/cs/script/shared/cs_protocol.lua
```

服务端权威逻辑：

```text
resources/demos/cs/script/server/init.lua
```

客户端连接、握手、输入上传逻辑：

```text
resources/demos/cs/script/client/init.lua
```

新增消息时建议按这个顺序修改：

1. 在 client 中增加 `send("<op>", payload)`。
2. 在 server 的 `handlers` 表里注册 `<op>` 处理函数。
3. server 用 `send(conn, "<reply_op>", payload, seq)` 返回结果。
4. 在 client 的 `handle_message()` 中处理 `<reply_op>`。

## 常见问题

`server did not listen on 127.0.0.1:7777`

端口被占用或 server 启动失败。换端口后同步修改 `resources/demos/cs/config/client/client.json`。

`CS_CLIENT_FAILED timeout`

client 已启动但没有完成协议闭环。检查 server 是否正在运行、地址端口是否一致、server 日志是否出现 `CS_SERVER_LISTENING`。

看不到 `CS_CLIENT_SUCCESS`

先用 `python scripts\build_release.py --smoke` 验证本机闭环，再检查手动启动时是否先启动 server 后启动 client。
