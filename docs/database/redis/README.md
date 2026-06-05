# Redis 模块

Redis 模块只在 server target 中启用。client 和 mobile target 不编译
`src/runtime/database/redis` 运行时代码，只保留公共配置结构。

## 启用方式

`resources/config/server/server.json` 控制是否启用 Redis：

```json
{
  "redis": "",
  "redis_required": false
}
```

- `redis` 为空时不启动 Redis worker。
- `redis` 非空时从该路径加载 Redis 详细配置，例如
  `resources/config/server/redis.json`。
- `redis_required=true` 时，配置必须有效，Redis runtime 必须已编译，并且启动时会等待初始连接成功。
- `redis_required=false` 时，Redis 失败不会阻止 server 启动；相关请求会按运行状态同步失败或排队。

`redis.json` 中的 `connection.password` 支持环境变量插值。配置 diff、日志和脚本配置查询不会输出明文密码。

## Lua API

Redis runtime 编译启用时，普通 VM owner 会导出全局 `redis` 表：

```lua
redis.command({"PING"}, function(result)
    if result.ok then
        log.info("redis ping: {}", result.value)
    else
        log.warn("redis failed: {}", result.error)
    end
end)

redis.eval(
    "return redis.call('GET', KEYS[1])",
    {"example:key"},
    {},
    function(result)
        if result.ok then
            log.info("value: {}", result.value)
        end
    end,
    { routing_key = "example:key" }
)
```

可用函数：

- `redis.command(argv, callback, options?)`
- `redis.eval(script, keys, args, callback, options?)`
- `redis.is_running()`
- `redis.is_healthy()`
- `redis.dispatch(max?)`

`callback` 在调用方 VM 所属线程 dispatch，不在 Redis worker 线程直接执行。`options.timeout_ms` 为 0 时使用配置中的 `connection.command_timeout_ms`。需要保持业务顺序时传入相同 `routing_key`；未指定 `routing_key` 的请求会按 worker 当前负载路由。

第一版拒绝 connection-state、blocking、Pub/Sub 和 transaction 类命令，例如 `AUTH`、`SELECT`、`QUIT`、`SUBSCRIBE`、`BLPOP`、`MULTI`、`EXEC` 等。AUTH 和 SELECT 只能由 Redis worker 根据配置完成。

## C++ API

普通 C++ 调用方通过全局单例访问：

```cpp
auto submit = redis::RedisClient::Instance().Command(
    {"PING"},
    [](redis::RedisResult&& result) {
        // This low-level completion runs on a Redis worker thread.
    });
```

低层 `RedisCompletion` 默认在 Redis worker 线程执行，不能直接访问外部 Lua VM 或非线程安全对象。需要回到调用方线程时，应通过 `ScriptVM` 的 `AsyncResultDispatcher` 包装 completion。

## 生命周期和观测

Engine 在主 `EventLoop` 创建后、Mongo / `DatabaseService` 初始化前初始化 Redis；关闭时新增 `RedisShutdown` 阶段，并在 VM owner 销毁前停止 Redis worker。

Admin HTTP readiness 会报告 Redis 状态：

- 未配置且非 required：disabled，不影响 readiness。
- 已配置但非 required 且不健康：degraded，不阻止 server 启动。
- required 且未 healthy：readiness 失败。

`/stats` 在 Redis runtime 编译启用时包含 Redis worker、队列、inflight、accepted、rejected、completed 和 timeout 统计。

## 测试

当前单元测试覆盖配置解析、`redis_required` 校验、Redis 配置默认值校验和密码 diff 脱敏：

```powershell
cmake --build artifacts\build --config Release --target test_config
artifacts\bin\Release\test_config.exe
```

Redis 集成测试应保持 opt-in。推荐使用以下环境变量启用本地 Redis 测试：

```powershell
$env:ENGINE_REDIS_TEST_ENABLED = "1"
$env:ENGINE_REDIS_TEST_URL = "redis://127.0.0.1:6379/0"
```

没有本地 Redis 时，CI 不应默认运行集成测试。
