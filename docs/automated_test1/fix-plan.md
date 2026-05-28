# 修复计划 — 自动化测试修复 (automated_test1)

## 优先级总览

| 优先级 | 类别 | 测试数 | 影响面 | 预估工作量 |
|--------|------|--------|--------|-----------|
| P0 | MongoDB 配置缺失 (超时) | 10 | 所有 Lua 测试 + smoke | 小 |
| P1 | 段错误 (空指针) | 4 | hotreload/orm/space/coroutine | 中 |
| P2 | Auth 会话验证 | 2 (8断言) | SessionManager | 中 |
| P3 | AOI/Space 实体逻辑 | 2 (5断言) | AOI/Space | 中 |
| P4 | Buffer/Engine/Network | 3 | buffer/engine/tcp | 小-中 |

---

## P0: MongoDB 配置缺失 — 10 个超时测试

### 问题分析

所有 Lua 测试 (`lua.*`) 和部分 smoke 测试 (`smoke.engine_init`, `smoke.lua`) 在引擎初始化时调用 `mongoc_client_pool_new_with_error()`，传入的 URI 为空字符串，导致断言失败后引擎挂起。

关键日志:
```
mongoc: Error parsing URI: 'Invalid URI, no scheme part specified'
mongoc_client_pool_new_with_error(): assertion failed: uri
```

### 修复方案

**方案 A (推荐):** 在测试配置中提供有效的 MongoDB URI 或允许跳过 MongoDB 初始化。
- 修改测试 fixture / CMake test 配置，设置环境变量 `EVPP_MONGO_URI` 或等效配置项
- 或在 `engine.cc` 中当 URI 为空时跳过 MongoDB 初始化而非断言崩溃

**方案 B:** 为测试环境设置 `library_mode=true` 且不初始化 Mongo。
- 检查 `smoke.engine_init` 和 Lua 测试的配置差异
- `unit.engine` 测试使用了 `library_mode=true` 但仍然失败，说明该路径也有问题

### 涉及文件
- `src/runtime/evpp/engine.cc` — Init() 中的 Mongo 初始化逻辑
- `src/tests/smoke/smoke_engine_init.cpp` — smoke 引擎测试
- `src/tests/lua/lua_runner/lua_test_runner.cpp` — Lua 测试运行器
- 各 Lua 测试脚本 `src/tests/lua/**/*.lua`
- CMake 测试配置 `src/tests/**/CMakeLists.txt`

### 验证方式
设置 `EVPP_MONGO_URI=mongodb://localhost:27017` 重新运行超时的10个测试。

---

## P1: 段错误 (SIGSEGV) — 4 个测试

### 问题分析

4 个测试在特定操作上触发空指针解引用。所有段错误都发生在测试用例的第一个断言附近，提示相关子系统未正确初始化。

### 逐个修复

#### 1. unit.hotreload — ReloadAll at test_hotreload.cpp:106
```
test cases: 11 | 10 passed | 1 failed
```
- 前面 10 个用例通过，只有 "ReloadAll with no target set" 崩溃
- **推测:** `ReloadAll()` 内部访问了空的 target 列表或未初始化的文件监视器
- **修复:** 在 `ReloadAll()` 中添加空检查

#### 2. unit.database.orm — FindById at test_orm.cpp:127
```
test cases:  7 |  6 passed | 1 failed
```
- "FindById on unregistered collection returns nullopt" 崩溃
- **推测:** 对未注册 collection 调用 FindById 时内部解引用了空指针
- **修复:** 在 ORM FindById 中添加 collection 存在性检查

#### 3. unit.space — Space creation at test_space.cpp:49
```
test cases:  4 |  3 passed | 1 failed
```
- 第一个用例 "Space creation with id and name" 就崩溃
- **推测:** Space 构造函数依赖某个未初始化的全局组件
- **修复:** 检查 Space 构造链路，确保依赖初始化

#### 4. unit.coroutine — CreateCoroutine at test_coroutine.cpp:129
```
test cases:  7 |  6 passed | 1 failed
```
- "CreateCoroutine with empty stack returns 0" 崩溃
- **推测:** 空栈/0 栈大小时创建协程触发内部空指针
- **修复:** 在 CreateCoroutine 中对 stack_size=0 做保护

### 涉及文件
- `src/runtime/evpp/hotreload*.h` / `hotreload*.cc`
- `src/runtime/evpp/database/orm*.h` / `orm*.cc`
- `src/runtime/evpp/space.h` / `space.cc`
- `src/runtime/evpp/coroutine.h` / `coroutine.cc`
- 对应测试文件

---

## P2: Auth 会话验证 — 2 个测试 (8 断言)

### 问题分析

`SessionManager::CreateSession()` 返回的 session 立即可用（无报错），但 `IsSessionValid()` 返回 false。可能原因:

1. **SessionManager 单例状态污染:** 之前测试未清理，当前 session 插入到了不同的内部 map
2. **会话创建时未正确写入内部存储:** `CreateSession` 返回了 session 结构体但未加入内部索引
3. **会话立即过期:** TTL 配置为 0 或极短，session 创建后瞬间过期

错误模式: 所有 8 个失败点全部是 `IsSessionValid(session.session_id) == false`

### 修复方案
1. 检查 `CreateSession` 实现，确认 session 创建后被正确加入内部 map
2. 检查 `IsSessionValid` 的查找逻辑是否与 `CreateSession` 使用相同的 key
3. 检查 TTL 默认值，确认不是 0
4. 在测试 `SetUp`/`TearDown` 中清理 SessionManager 单例

### 涉及文件
- `src/runtime/evpp/auth/session_manager.h`
- `src/runtime/evpp/auth/session_manager.cc`
- `src/tests/unit/auth/test_auth.cpp`
- `src/tests/integration/entity/test_auth_entity.cpp`

---

## P3: AOI/Space 实体 — 2 个测试 (5 断言)

### 3.1 AOI 可见性 (integration.entity.aoi)

3 个断言失败:
- 两个在范围内的实体互相不可见 (`aSeesB == false`)
- 移除实体后可见性查询返回空 (`vis100.size() == 0`)
- AOI 事件未触发 (`events_for_1 == 0`)

**推测:** AOI 组件的实体注册/位置更新未生效，或 radius 参数未正确传递。

### 3.2 Space 实体状态 (integration.space.entity)

2 个断言失败:
- `GetState() == Active (1)` 而不是预期的 `Created (0)` — 实体状态机跳过了 Created 状态
- 跨 Space 查询返回了另一个 Space 的实体 — 隔离被破坏

**推测:** Entity 创建后立即被激活（可能是定时器/帧循环副作用），且 Space 的实体索引使用了全局而非按 Space 隔离的存储。

### 涉及文件
- `src/runtime/evpp/entity/aoi*.h/cc`
- `src/runtime/evpp/space.h/cc`
- `src/runtime/evpp/entity/entity.h/cc`
- 测试文件

### 修复方案
1. 检查 AOI 系统中实体注册流程 (AddEntity / UpdatePosition)
2. 检查 Entity 状态机: Created → Active 转换的触发条件
3. 检查 Space::GetEntity 的查找是否限定了 Space 范围

---

## P4: Buffer/Engine/Network — 3 个测试

### 4.1 unit.buffer — buffer.h:81

`Assertion failed: len <= length()` — 试图读取超出 buffer 长度的数据。
**修复:** 检查 buffer 的读写边界检查逻辑。

### 4.2 unit.engine — 测试失败

`library_mode=true` 路径下引擎初始化后测试失败。日志显示 engine 初始化看似正常完成。
**修复:** 需要查看具体的 Catch2 失败输出来定位。

### 4.3 integration.network.tcp — tcp_server.cc:30

`Assertion failed: connections_.empty()` — TCP 服务器析构时仍有未清理的连接。
**修复:** 在析构前显式关闭所有连接，或将断言改为清理逻辑。

### 涉及文件
- `src/runtime/evpp/buffer.h`
- `src/runtime/evpp/engine.cc`
- `src/runtime/evpp/tcp_server.cc`

---

## 建议修复顺序

```
Phase 1 (P0): MongoDB 配置 → 恢复 10 个测试 (预估 1-2h)
Phase 2 (P1): 段错误修复  → 恢复  4 个测试 (预估 2-3h)
Phase 3 (P2): Auth 会话    → 恢复  2 个测试 (预估 1-2h)
Phase 4 (P3): AOI/Space    → 恢复  2 个测试 (预估 2-3h)
Phase 5 (P4): 杂项修复    → 恢复  3 个测试 (预估 1-2h)
```

全部修复后预期: **44/44 (100%)** 通过。

---

## 回归风险

| 修复范围 | 风险 |
|----------|------|
| engine.cc Mongo 初始化 | 可能影响生产环境启动行为 |
| SessionManager 内部存储 | 影响所有登录/鉴权流程 |
| AOI 可见性逻辑 | 影响游戏内实体可见性广播 |
| Space 隔离 | 影响多场景/副本隔离 |
| HotReload | 影响开发期热重载功能 |
| Coroutine | 影响 Lua 协程调度 |
