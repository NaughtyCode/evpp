# 自动化测试结果 — 2026/05/28

**分支:** server_engine2 | **配置:** Debug | **并行数:** 4 | **总耗时:** ~900s

## 总览

| 指标 | 数值 |
|------|------|
| 总测试数 | 44 |
| 通过 | 23 (52%) |
| 失败 | 21 (48%) |
| 超时 | 10 |
| 段错误 | 4 |
| 断言失败 | 7 |

---

## 类别一：超时/挂起 (10 tests)

**共同根因:** 引擎初始化时 MongoDB URI 为空，mongoc 断言失败后引擎挂起。

```
mongoc: Error parsing URI: 'Invalid URI, no scheme part specified'
mongoc_client_pool_new_with_error(): assertion failed: uri
```

| # | 测试名 | 超时(s) | 标签 |
|---|--------|---------|------|
| 1 | smoke.engine_init | 30.03 | smoke |
| 36 | lua.msgpack | 30.03 | lua |
| 37 | lua.net.client_server | Failed (860s) | lua, network |
| 38 | lua.net.udp | 60.03 | lua, network |
| 39 | lua.net.kcp | 60.01 | lua, network |
| 40 | lua.timer.once | 30.02 | lua |
| 41 | lua.timer.interval | 30.03 | lua |
| 42 | lua.log | 15.03 | lua |
| 43 | lua.import | 15.01 | lua |
| 44 | smoke.lua | 15.02 | lua, smoke |

**受影响文件:** `src/runtime/evpp/engine.cc` (Init 流程), MongoDB 配置

---

## 类别二：段错误 SIGSEGV (4 tests)

| # | 测试名 | 崩溃位置 | 测试用例 |
|---|--------|----------|----------|
| 19 | unit.hotreload | test_hotreload.cpp:106 | ReloadAll with no target set |
| 20 | unit.database.orm | test_orm.cpp:127 | FindById on unregistered collection |
| 25 | unit.coroutine | test_coroutine.cpp:129 | CreateCoroutine with empty stack |
| 24 | unit.space | test_space.cpp:49 | Space creation with id and name |

### 详细堆栈

**unit.hotreload** — `SIGSEGV` in ReloadAll:
```
test cases: 11 | 10 passed | 1 failed
assertions: 13 | 12 passed | 1 failed
```

**unit.database.orm** — `SIGSEGV` in FindById:
```
test cases:  7 |  6 passed | 1 failed
assertions: 23 | 22 passed | 1 failed
```

**unit.space** — `SIGSEGV` in Space creation:
```
test cases:  4 |  3 passed | 1 failed
assertions: 12 | 11 passed | 1 failed
```

**unit.coroutine** — `SIGSEGV` in CreateCoroutine:
```
test cases:  7 |  6 passed | 1 failed
assertions: 13 | 12 passed | 1 failed
```

---

## 类别三：断言失败 (7 tests)

### 3.1 Auth 会话验证失败 (2 tests, 8 assertions)

**unit.auth** — 6 个用例失败:
```
SessionManager validates a newly created session      — test_auth.cpp:275
SessionManager revoke by session ID                   — test_auth.cpp:332
SessionManager revoke by connection                   — test_auth.cpp:349
SessionManager CleanupExpired removes expired sessions — test_auth.cpp:407
SessionManager max sessions per account               — test_auth.cpp:425
SessionManager validates session through backend       — test_auth.cpp:467
```
全部表现为: `IsSessionValid(session.session_id) == false`

**integration.auth.entity** — 2 个用例失败:
```
SessionManager cleanup removes expired sessions — test_auth_entity.cpp:93
Multiple sessions for different entities       — test_auth_entity.cpp:114
```

### 3.2 AOI 可见性失败 (1 test, 3 assertions)

**integration.entity.aoi**:
```
Multiple entities at different positions         — test_entity_aoi.cpp:163: aSeesB == false
Remove entity from AOI                           — test_entity_aoi.cpp:200: vis100.size() == 0, expected 1
AOI events are fired symmetrically               — test_entity_aoi.cpp:325: events_for_1 == 0, expected >= 1
```

### 3.3 Space 实体状态/隔离失败 (1 test, 2 assertions)

**integration.space.entity**:
```
Space CreateEntity returns valid entity            — test_space_entity.cpp:117: GetState() == 1 (Active), expected 0 (Created)
Entities in different spaces are fully isolated    — test_space_entity.cpp:198: alpha->GetEntity(b1->GetId()) != nullptr
```

### 3.4 Buffer 断言 (1 test)

**unit.buffer** — `Assertion failed: len <= length()` in `buffer.h:81`

### 3.5 Engine 失败 (1 test)

**unit.engine** — 引擎初始化后测试失败 (library_mode=true 路径)

### 3.6 网络连接清理 (1 test)

**integration.network.tcp** — `Assertion failed: connections_.empty()` in `tcp_server.cc:30`

---

## 通过测试清单 (23 tests)

```
smoke.scriptvm                  unit.buffer (部分)
smoke.config_load               unit.config
smoke.tcp_loopback              unit.timer
unit.scriptvm                   unit.sandbox
unit.lual_error_safety          unit.runinloop_safety
unit.message_limits             unit.network.length_prefixed_codec
unit.network.rate_limiter       unit.entity
unit.rpc                        unit.database.cache
unit.metrics                    unit.aoi
unit.client_api                 integration.network.udp
integration.entity.net          integration.orm.entity
integration.client_api.tcp      integration.network.conn_limit
```
