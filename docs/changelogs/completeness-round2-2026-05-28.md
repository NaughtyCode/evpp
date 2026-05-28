# Changelog

## [Unreleased] — 2026-05-28

### Fixed — HTTP Pending Refs O(n) → O(1)
- `net_http_bind.cc:81`: 将 `std::find()` (O(n) 线性遍历) 替换为 `std::unordered_set::find()` (O(1) 哈希查找)。高并发 HTTP 场景下每个响应不再触发 O(n) 扫描。

### Added — AOI 子系统解除 #if 0 编译守卫
- `script_bind.cc:51-56`: 移除 AOI 导出的 `#if 0` 守卫，AOI 源码已在 CMakeLists 中编译，现在正式导出 `aoi` 全局表到 Lua。
- `CMakeLists.txt`: 添加 `aoi_bind.cc/h` 到 `SCRIPT_SOURCES`。

### Added — ORM + Cache 子系统编译与 Lua 绑定
- `CMakeLists.txt`: 新增 `ORM_SOURCES`（orm.cc, orm.h, cache.h），在 `ENGINE_MONGODB_ENABLED` 条件下编译。
- `CMakeLists.txt`: `orm_bind.cc/h` 在 `ENGINE_MONGODB_ENABLED` 条件下加入构建。
- `script_bind.cc`: ORM 导出在 `ENGINE_MONGODB_ENABLED` 守卫下调用。
- `script_bind.h`: `ExportOrm` 声明添加 `ENGINE_MONGODB_ENABLED` 守卫。

### Fixed — orm_bind.cc 索引字段重复推入 Bug
- `orm_bind.cc:57-59`: 修复 `l_orm_define` 中 `IndexDef::fields` 的字段被重复 push_back 的问题（原代码在 if 内 push 一次，if 外又 push 一次）。

### Added — RPC Lua 绑定
- 新建 `src/runtime/script/rpc_bind.h`、`rpc_bind.cc`: 导出 `rpc` 全局表，提供 `start_server`、`register_service`、`stop_server`、`start_client`、`call`、`stop_client` 六个 Lua API。
- `CMakeLists.txt`: 新增 `RPC_SOURCES`、`AUTH_SOURCES`，加入 `SERVERENGINE_SOURCES`。
- `script_bind.h/cc`: 声明并调用 `ExportRpc`。

### Added — Auth Lua 绑定
- 新建 `src/runtime/script/auth_bind.h`、`auth_bind.cc`: 导出 `auth` 全局表，提供 `set_token_backend`、`add_token`、`authenticate`、`create_session`、`validate_session`、`revoke_session`、`cleanup_expired` 七个 Lua API。
- `script_bind.h/cc`: 声明并调用 `ExportAuth`。

### Optimized — class.lua isinstanceof O(depth) → O(1)
- `resources/script/runtime/common/class.lua`: `isinstanceof` 从沿 `__index` 链逐级遍历改为基于预计算的 `__ancestors` 哈希表做 O(1) 查找。类创建和热重载路径均会重建祖先集合。

### Added — Config Reload 回调订阅
- `engine.cc:288-300`: 在 `Engine::Init()` 末尾注册 ConfigManager 的 reload 回调，日志级别、帧间隔、沙箱级别变更时输出日志。

### Added — 集成测试补充
- 新建 `test_entity_net.cpp`: Entity + TCP Connection 集成测试（连接绑定、实体销毁、EntityManager 计数）— 6 个测试用例。
- 新建 `test_auth_entity.cpp`: Auth + Entity 集成测试（会话创建/验证/撤销、TokenAuth、多实体会话）— 5 个测试用例。
- 新建 `test_orm_entity.cpp`: ORM + Entity Cache 集成测试（LRU 缓存、命中率、Schema 注册、缓存清理）— 7 个测试用例。该测试在 `ENGINE_MONGODB_ENABLED` 条件下编译。
- 守卫 ORM 单元测试（`test_orm`）和集成测试（`test_integ_orm_entity`）在 `ENGINE_MONGODB_ENABLED` 条件下编译，修复非 MongoDB 构建的链接错误。

### Fixed — test_entity_aoi.cpp Windows 宏冲突
- `test_entity_aoi.cpp:44`: 变量名 `far` 与 Windows `<windows.h>` 中的 `far` 宏冲突，重命名为 `far_away`。

### Build — CloudEngine 编译验证通过
- CloudEngine.dll Debug 构建成功，所有新增源文件正确编译链接。
