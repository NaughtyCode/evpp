# mongo-c-driver 2.3.0 技术报告

## 1. 概览

mongo-c-driver 是 MongoDB 官方 C 语言驱动，版本 **2.3.0**。本报告基于 `src/thirdparty/mongo-c-driver` 目录下的源码分析整理。

### 1.1 项目组成

项目由以下核心库构成：

| 库名 | 路径 | 说明 |
|------|------|------|
| **libbson** | `src/libbson/` | BSON 序列化/反序列化库，可独立使用 |
| **libmongoc** | `src/libmongoc/` | MongoDB 客户端驱动库，依赖 libbson |
| **common** | `src/common/` | 两个库共享的内部工具库（原子操作、Base64、MD5、OID、字符串、线程等） |
| **kms-message** | `src/kms-message/` | KMS 请求消息生成库（AWS KMS、Azure Key Vault、GCP KMS） |

### 1.2 版本信息

```
VERSION_CURRENT: 2.3.0
构建系统: CMake (>= 3.15)
语言标准: C99
许可证: Apache 2.0
```

---

## 2. 目录结构

### 2.1 顶层结构

```
mongo-c-driver/
├── CMakeLists.txt              # 根构建文件（543行）
├── VERSION_CURRENT             # 版本号: 2.3.0
├── COPYING                     # Apache 2.0 许可证
├── NEWS                        # 发布说明（145KB）
├── README.rst                  # 项目 README
├── CONTRIBUTING.md             # 贡献指南
├── THIRD_PARTY_NOTICES         # 第三方声明
├── Earthfile                   # Earthly 容器化构建（700行）
├── pyproject.toml              # Python 开发工具配置
├── uv.lock                     # Python 依赖锁
│
├── build/                      # CMake 构建辅助（22个模块）
│   ├── cmake/                  # CMake 模块
│   ├── bottle.py               # 测试用 HTTP 服务器
│   ├── mongodl.py              # MongoDB 下载工具
│   └── ...
│
├── docs/                       # 文档源
│   └── dev/                    # 开发者文档（Sphinx RST）
│
├── etc/                        # 配置与合规
│   ├── cyclonedx.sbom.json     # CycloneDX SBOM
│   ├── purls.txt               # 包 URL 清单
│   ├── ssdlc.md                # SSDLC 合规报告
│   └── third_party_vulnerabilities.md
│
├── src/                        # 源代码（见下文）
└── tools/                      # 构建工具
```

### 2.2 源码目录详析

```
src/
├── CMakeLists.txt              # 聚合构建文件
├── c-check.c                   # C public header 编译检测
├── cpp-check.cpp               # C++ header 兼容性检测
│
├── common/                     # 共享工具库（21个源文件）
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── common-atomic.{c,private.h}
│   │   ├── common-b64.{c,private.h}
│   │   ├── common-bits-private.h
│   │   ├── common-bson-dsl-private.h
│   │   ├── common-config.h.in
│   │   ├── common-json.{c,private.h}
│   │   ├── common-macros-private.h
│   │   ├── common-md5.{c,private.h}
│   │   ├── common-oid.{c,private.h}
│   │   ├── common-prelude.h
│   │   ├── common-string.{c,private.h}
│   │   ├── common-thread.{c,private.h}
│   │   ├── common-utf8-private.h
│   │   └── mlib/               # 现代 C 工具库（纯头文件）
│   │       ├── ckdint.h        # 溢出检测整数运算
│   │       ├── cmp.h, str.h, str_vec.h
│   │       ├── duration.h, time_point.h, timer.h
│   │       ├── intencode.h, intutil.h
│   │       ├── loop.h, platform.h
│   │       └── vec.th          # 模板化向量容器
│   └── tests/                  # 3 个测试文件
│
├── libbson/                    # BSON 库（独立子项目）
│   ├── CMakeLists.txt          # 407 行
│   ├── libbson.rc.in
│   ├── doc/                    # Sphinx 文档（301 个 RST 文件）
│   ├── etc/bsonConfig.cmake    # CMake 包配置文件
│   ├── examples/               # 10 个示例程序
│   ├── fuzz/                   # 6 个模糊测试文件
│   ├── src/
│   │   ├── bson/               # BSON 核心实现（49 个源文件）
│   │   │   ├── bson.c, bson.h, bson_t.h
│   │   │   ├── bson-iter.c, bson-json.c, bson-oid.c
│   │   │   ├── bson-reader.c, bson-writer.c
│   │   │   ├── bson-decimal128.c, bson-iso8601.c
│   │   │   ├── bson-utf8.c, bson-value.c
│   │   │   ├── bson-clock.c, bson-vector.c
│   │   │   ├── bson-bcon.c, bson-keys.c, bson-string.c
│   │   │   ├── bson-timegm.c, bson-context.c
│   │   │   ├── bson-version-functions.c
│   │   │   ├── error.c, memory.c, validate.c
│   │   │   ├── config.h.in, version.h.in
│   │   │   └── compat.h, macros.h
│   │   └── jsonsl/             # 内嵌 JSON 解析器
│   │       ├── jsonsl.c, jsonsl.h, LICENSE
│   └── tests/                  # 完整测试套件
│       ├── 21 个测试 C 文件
│       ├── binary/              # 48+ 个 .bson 测试数据
│       └── json/                # JSON 测试数据（bson_corpus 等）
│
├── libmongoc/                  # MongoDB 客户端驱动（核心）
│   ├── CMakeLists.txt          # 1418 行
│   ├── libmongoc.rc.in
│   ├── doc/                    # Sphinx 文档（797 个 RST 文件）
│   ├── etc/mongocConfig.cmake.in
│   ├── examples/               # 48+ 个示例程序
│   ├── src/mongoc/             # 驱动核心实现
│   │   ├── 117 个 .c 文件
│   │   ├── 162 个 .h 文件（公开+私有）
│   │   ├── uthash.h, utlist.h  # 内嵌 uthash 2.3.0
│   │   └── mongoc-config.h.in
│   └── tests/                  # 大规模测试套件
│       ├── 100+ 个测试 C 文件
│       ├── json/               # 50+ 个 JSON 测试规范目录
│       ├── mock_server/        # 模拟服务器框架
│       ├── unified/            # 统一测试格式运行器
│       └── x509gen/            # X.509 证书测试数据
│
├── kms-message/                # KMS 消息库（独立子项目）
│   ├── CMakeLists.txt          # 313 行
│   ├── README.md
│   ├── COPYING
│   ├── cmake/                  # kms_message-config.cmake, pkg-config
│   ├── src/
│   │   ├── 24 个 .c 文件
│   │   ├── 31 个 .h 文件
│   │   └── kms_message/        # 15 个公开 API 头文件
│   ├── test/                   # 测试（含 AWS SigV4 测试套件）
│   └── aws-sig-v4-test-suite/  # AWS 签名 V4 测试向量（30 个测试用例）
│
├── utf8proc-2.8.0/             # 内嵌 utf8proc（4 个文件）
│   ├── LICENSE.md
│   ├── utf8proc.c              # 31KB 实现
│   ├── utf8proc.h              # 31KB 头文件
│   └── utf8proc_data.c         # 1.9MB Unicode 数据表
│
├── uthash/uthash-2.3.0/        # 内嵌 uthash（2 个头文件）
│   ├── uthash.h
│   └── utlist.h
│
├── zlib-1.3.1/                 # 内嵌 zlib（44 个文件）
│   ├── adler32.c, crc32.c, deflate.c
│   ├── inflate.c, inffast.c, inftrees.c
│   ├── compress.c, uncompr.c
│   ├── gzlib.c, gzread.c, gzwrite.c, gzclose.c
│   ├── trees.c, zutil.c
│   ├── zlib.h, zconf.h, zconf.h.in, zconf.h.cmakein
│   ├── CMakeLists.txt, Makefile.in, configure
│   └── ...
│
└── tools/                      # 工具
    └── mongoc-stat.c           # 性能计数器显示工具
```

---

## 3. 核心库详解

### 3.1 libbson

**功能**: 纯 C 语言的 BSON 文档序列化/反序列化库，完全独立于 libmongoc。

**核心模块**:

| 模块 | 文件 | 功能 |
|------|------|------|
| BSON 核心 | `bson.c`, `bson.h`, `bson_t.h` | BSON 文档创建、销毁、操作 |
| 迭代器 | `bson-iter.c/h` | BSON 文档遍历 |
| JSON 互转 | `bson-json.c/h`, `jsonsl.c/h` | BSON ↔ JSON 转换 |
| 对象 ID | `bson-oid.c/h` | MongoDB ObjectID 生成与操作 |
| 读写器 | `bson-reader.c/h`, `bson-writer.c/h` | 序列化读写 |
| Decimal128 | `bson-decimal128.c/h` | IEEE 754-2008 decimal128 支持 |
| UTF-8 | `bson-utf8.c/h` | UTF-8 验证与处理 |
| ISO 8601 | `bson-iso8601.c/h` | 日期时间解析 |
| BCON | `bson-bcon.c/h` | C 风格的 BSON 构造语法糖 |
| 时钟 | `bson-clock.c/h` | 单调时钟 |
| 向量 | `bson-vector.c/h` | 动态数组 |
| 上下文 | `bson-context.c/h` | 内存管理上下文 |
| 内存/错误 | `memory.c/h`, `error.c/h` | 内存分配、错误管理 |
| 验证 | `validate.c/h` | BSON 文档验证 |

**公开 API 头文件（24个）**: `bson.h`（总入口）, `bson-bcon.h`, `bson-clock.h`, `bson-context.h`, `bson-decimal128.h`, `bson-error.h`, `bson-iter.h`, `bson-json.h`, `bson-keys.h`, `bson-oid.h`, `bson-reader.h`, `bson-string.h`, `bson-types.h`, `bson-utf8.h`, `bson-value.h`, `bson-vector.h`, `bson-writer.h` 等。

**构建产物**:
- `bson_static` → `bson-2.0.lib`（静态库）
- `bson_shared` → `bson-2.0.dll`（动态库）
- 安装头文件到 `include/bson-2.0/`
- 生成 `bson-2.0.pc` pkg-config 文件

### 3.2 libmongoc

**功能**: 完整的 MongoDB 客户端 C 驱动，功能覆盖 MongoDB 6.0+ 协议。

**核心模块分类**:

#### 3.2.1 客户端与连接

| 模块 | 文件 | 说明 |
|------|------|------|
| 客户端 | `mongoc-client.c/h` | 核心客户端 API，连接管理 |
| 客户端池 | `mongoc-client-pool.c/h` | 连接池 |
| 会话 | `mongoc-client-session.c/h` | 因果一致性会话 |
| URI 解析 | `mongoc-uri.c/h` | MongoDB 连接字符串解析 |
| 握手 | `mongoc-handshake.c/h` | 连接握手与元数据 |
| 初始化 | `mongoc-init.c/h` | 库初始化/清理 |

#### 3.2.2 集群与拓扑

| 模块 | 文件 | 说明 |
|------|------|------|
| 集群 | `mongoc-cluster.c/h` | 集群管理（副本集/分片） |
| 拓扑 | `mongoc-topology.c/h` | 拓扑发现与维护 |
| 拓扑扫描 | `mongoc-topology-scanner.c` | 服务器扫描 |
| 拓扑描述 | `mongoc-topology-description.c/h` | 拓扑状态描述 |
| 后台监控 | `mongoc-topology-background-monitoring.c` | 后台拓扑监控 |
| 服务器描述 | `mongoc-server-description.c/h` | 单台服务器描述 |
| 服务器监控 | `mongoc-server-monitor.c/h` | 定期服务器健康检查 |
| 主机列表 | `mongoc-host-list.c/h` | 主机+端口列表管理 |

#### 3.2.3 操作接口

| 模块 | 文件 | 说明 |
|------|------|------|
| 数据库 | `mongoc-database.c/h` | 数据库操作 |
| 集合 | `mongoc-collection.c/h` | 集合操作（CRUD） |
| 游标 | `mongoc-cursor.c/h` + 4 个子类 | 查询结果遍历 |
| 批量操作 | `mongoc-bulk-operation.c/h` | 批量写入 |
| BulkWrite | `mongoc-bulkwrite.c/h` | 现代批量写入 |
| 聚合 | `mongoc-aggregate.c` | 聚合管道 |
| 变更流 | `mongoc-change-stream.c/h` | 实时数据变更监听 |
| 查找与修改 | `mongoc-find-and-modify.c/h` | findAndModify 操作 |
| 写入命令 | `mongoc-write-command.c` | 写入命令执行 |
| 读偏好 | `mongoc-read-prefs.c/h` | 读偏好配置 |
| 读关注 | `mongoc-read-concern.c/h` | 读关注级别 |
| 写关注 | `mongoc-write-concern.c/h` | 写关注级别 |
| GridFS | `mongoc-gridfs*.c/h` (8 个文件) | 大文件存储 |
| 命令执行 | `mongoc-cmd.c`, `mongoc-opcode.c` | MongoDB 协议命令 |
| RPC 层 | `mcd-rpc.c`, `mongoc-rpc.c` | 底层 RPC 通信 |
| 服务器 API | `mongoc-server-api.c/h` | 服务器 API 版本固定 |

#### 3.2.4 网络与流

| 模块 | 文件 | 说明 |
|------|------|------|
| 流抽象 | `mongoc-stream.c/h` | 通用流接口 |
| 缓冲流 | `mongoc-stream-buffered.c/h` | 缓冲流包装器 |
| Socket 流 | `mongoc-stream-socket.c/h` | TCP Socket 流 |
| 文件流 | `mongoc-stream-file.c/h` | 文件流 |
| GridFS 流 | `mongoc-stream-gridfs*.c/h` (3 个文件) | GridFS 流式读写 |
| Socket | `mongoc-socket.c/h` | TCP 套接字封装 |
| 服务器流 | `mongoc-server-stream.c` | 服务器连接流 |
| 超时控制 | `mongoc-timeout.c` | 操作超时 |

#### 3.2.5 TLS/SSL 安全传输

支持三种 TLS 后端，按平台自动选择：

| 后端 | 文件 | 平台 |
|------|------|------|
| **Secure Channel (SChannel)** | `mongoc-secure-channel.c` | Windows 原生 |
| **Secure Transport** | `mongoc-secure-transport.c` | macOS/iOS 原生 |
| **OpenSSL** | `mongoc-openssl.c`, `mongoc-stream-tls-openssl*.c` | Linux/通用 |

TLS 流统一接口: `mongoc-stream-tls.c/h`

#### 3.2.6 认证

| 模块 | 文件 | 说明 |
|------|------|------|
| 认证框架 | `mongoc-cluster-sasl.c` | SASL 认证高层逻辑 |
| SCRAM 认证 | `mongoc-scram.c` | SCRAM-SHA-1/256 认证 |
| GSSAPI/SSPI | `mongoc-cluster-sspi.c`, `mongoc-sspi.c`, `mongoc-cluster-cyrus.c`, `mongoc-cyrus.c` | Kerberos 认证 |
| MONGODB-AWS | `mongoc-cluster-aws.c` | AWS IAM 认证 |
| MONGODB-OIDC | `mongoc-cluster-oidc.c`, `mongoc-oidc-cache.c`, `mongoc-oidc-callback.c`, `mongoc-oidc-env.c` | OpenID Connect 认证 |
| HTTP 辅助 | `mongoc-http.c` | OIDC token 获取 |

#### 3.2.7 加密（Client-Side Encryption）

| 模块 | 文件 | 说明 |
|------|------|------|
| CSE 主模块 | `mongoc-client-side-encryption.c/h` | 客户端字段级加密 |
| Crypt 接口 | `mongoc-crypt.c` | libmongocrypt 绑定 |
| KMS 集成 | kms-message 库 | AWS/Azure/GCP KMS 请求 |

#### 3.2.8 压缩

| 算法 | 依赖 | 说明 |
|------|------|------|
| Zlib | zlib-1.3.1（内嵌）| DEFLATE 压缩 |
| Zstd | libzstd（系统）| Zstandard 压缩 |
| Snappy | libsnappy（系统）| Snappy 压缩 |

#### 3.2.9 其他模块

| 模块 | 文件 | 说明 |
|------|------|------|
| 错误处理 | `mongoc-error.c/h` | 错误域和错误码 |
| 日志 | `mongoc-log.c/h`, `mongoc-structured-log.c/h` | 日志框架、结构化日志 |
| 监控 (APM) | `mongoc-apm.c/h` | 命令事件监听 |
| 计数器 | `mongoc-counters.c` | 性能计数器 |
| 重试 | `mongoc-retryable-cmd.c`, `mongoc-retry-backoff-generator.c` | 可重试命令与退避 |
| 中断 | `mongoc-interrupt.c` | 操作中断 |
| 异步 | `mongoc-async.c`, `mongoc-async-cmd.c` | 异步操作 |
| 集合管理 | `mongoc-set.c` | 集合操作工具 |
| 共享内存 | `mongoc-shared.c` | 共享内存性能计数器 |
| 缓冲/队列 | `mongoc-buffer.c`, `mongoc-queue.c` | 缓冲区和队列 |

**构建产物**:
- `mongoc_static` → `mongoc-2.0.lib`
- `mongoc_shared` → `mongoc-2.0.dll`
- 导出目标: `bson::static`, `mongo::detail::c_dependencies`, `mongo::detail::c_tls_backend`, `mongo::detail::c_sasl_backend`

### 3.3 common 共享库

所有 private 接口，用于 libbson 和 libmongoc 内部：

| 模块 | 文件 | 功能 |
|------|------|------|
| 原子操作 | `common-atomic.c/h` | 跨平台原子操作 |
| Base64 | `common-b64.c/h` | Base64 编解码 |
| 位操作 | `common-bits-private.h` | 位操作宏 |
| BSON DSL | `common-bson-dsl-private.h` | BSON 构建 DSL 宏 |
| JSON | `common-json.c/h` | JSON 解析辅助 |
| MD5 | `common-md5.c/h` | MD5 哈希 |
| OID | `common-oid.c/h` | 通用 OID 工具 |
| 字符串 | `common-string.c/h` | 字符串处理 |
| 线程 | `common-thread.c/h` | 跨平台线程/Mutex/Once |
| UTF-8 | `common-utf8-private.h` | UTF-8 实用函数 |
| 数学库 | `mlib/` | checked int, duration, str, vec 等现代 C 工具 |

名称修饰机制：通过 `MCOMMON_NAME_PREFIX` 宏分别导出为 `_bson_mcommon_*` 和 `_mongoc_mcommon_*`，避免符号冲突。

### 3.4 kms-message

| 功能 | 说明 |
|------|------|
| AWS KMS | 请求签名，AWS SigV4 签名 |
| Azure Key Vault | OAuth2 访问令牌、解密请求 |
| GCP KMS | 服务账户认证、解密请求 |
| KMIP | KMIP 协议读写和响应解析 |

---

## 4. 内嵌第三方依赖

| 依赖 | 版本 | 位置 | 大小 | 许可证 |
|------|------|------|------|------|
| **zlib** | 1.3.1 | `src/zlib-1.3.1/` | 44 文件 | Zlib |
| **utf8proc** | 2.8.0 | `src/utf8proc-2.8.0/` | 4 文件 | MIT + Unicode |
| **uthash** | 2.3.0 | `src/uthash/` + `src/libmongoc/src/mongoc/` | 2 头文件 | BSD-1-Clause |
| **jsonsl** | (无版本) | `src/libbson/src/jsonsl/` | 3 文件 | MIT |

内嵌依赖通过 `etc/purls.txt` 和 `etc/cyclonedx.sbom.json` 进行 SBOM 管理。

---

## 5. 配置系统

### 5.1 config.h 生成

两个库各自生成 `config.h`：

- **libbson** → `bson/config.h`
  - 来自 `src/libbson/src/bson/config.h.in`
  - 13 个构建时检测宏（字节序、OS类型、可用函数等）

- **libmongoc** → `mongoc/mongoc-config.h`
  - 来自 `src/libmongoc/src/mongoc/mongoc-config.h.in`
  - 40+ 个构建时配置宏（TLS后端、压缩、加密等）

- **common** → `common-config.h`
  - 来自 `src/common/src/common-config.h.in`

### 5.2 主要构建选项

| 选项 | 取值 | 默认值 | 说明 |
|------|------|--------|------|
| `ENABLE_MONGOC` | BOOL | ON | 构建 libmongoc |
| `ENABLE_STATIC` | BOOL/BUILD_ONLY | ON | 构建静态库 |
| `ENABLE_SHARED` | BOOL | ON | 构建动态库 |
| `ENABLE_SSL` | WINDOWS/DARWIN/OPENSSL/OFF/AUTO | AUTO | TLS 后端选择 |
| `ENABLE_SASL` | CYRUS/SSPI/OFF/AUTO | AUTO | SASL 认证后端 |
| `ENABLE_ZLIB` | BUNDLED/SYSTEM/OFF | BUNDLED | zlib 压缩 |
| `ENABLE_ZSTD` | ON/AUTO/OFF | AUTO | zstd 压缩 |
| `ENABLE_SNAPPY` | ON/AUTO/OFF | AUTO | Snappy 压缩 |
| `ENABLE_SRV` | BOOL | ON | mongodb+srv:// URI |
| `ENABLE_CLIENT_SIDE_ENCRYPTION` | ON/AUTO/OFF | AUTO | 客户端字段级加密 |
| `ENABLE_MONGODB_AWS_AUTH` | ON/AUTO/OFF | AUTO | AWS IAM 认证 |
| `ENABLE_TESTS` | BOOL | — | 构建测试 |
| `ENABLE_EXAMPLES` | BOOL | — | 构建示例 |
| `ENABLE_MAINTAINER_FLAGS` | BOOL | — | 严格编译检查 |
| `ENABLE_TRACING` | BOOL | — | 运行时跟踪 |
| `ENABLE_SHM_COUNTERS` | BOOL | — | 共享内存性能计数器 |
| `ENABLE_DEBUG_ASSERTIONS` | BOOL | — | 调试断言 |
| `ENABLE_PIC` | BOOL | ON (非Windows) | 位置无关代码 |
| `MONGO_SANITIZE` | STRING | address,undefined (devel) | 清除器选项 |
| `USE_SYSTEM_LIBBSON` | BOOL | OFF | 使用系统 libbson |

### 5.3 平台自动检测

- **TLS 后端**: Windows→SecureChannel, macOS→SecureTransport, 其他→OpenSSL
- **SASL 后端**: Windows→SSPI, 其他→Cyrus SASL
- **DNS 解析**: Windows→Dnsapi.dll, 其他→libresolv (res_nsearch/res_search)
- **加密后端**: 与 TLS 后端完全对应
- **性能计数器**: Linux/ARM macOS→shm_open, Intel CPU→RDTSCP

---

## 6. 公共 API 接口

### 6.1 libbson 公共 API

| 类别 | 关键类型和函数 |
|------|---------------|
| 文档 | `bson_t`, `bson_new()`, `bson_destroy()`, `bson_copy_to()` |
| 追加 | `bson_append_int32()`, `bson_append_utf8()`, `bson_append_document()` 等 |
| 迭代 | `bson_iter_t`, `bson_iter_init()`, `bson_iter_next()` |
| JSON | `bson_as_json()`, `bson_init_from_json()` |
| OID | `bson_oid_t`, `bson_oid_init()` |
| Decimal128 | `bson_decimal128_t` |
| 读写器 | `bson_reader_t`, `bson_writer_t` |
| BCON | `BCON_NEW()` 宏 |
| 时钟 | `bson_clock_gettime()` |

### 6.2 libmongoc 公共 API

| 类别 | 关键类型和函数 |
|------|---------------|
| 客户端 | `mongoc_client_t`, `mongoc_client_new()` |
| 连接池 | `mongoc_client_pool_t`, `mongoc_client_pool_new()` |
| 数据库 | `mongoc_database_t`, `mongoc_client_get_database()` |
| 集合 | `mongoc_collection_t`, `mongoc_collection_find_with_opts()` |
| 游标 | `mongoc_cursor_t`, `mongoc_cursor_next()` |
| 批量操作 | `mongoc_bulk_operation_t` |
| GridFS | `mongoc_gridfs_bucket_t` |
| 变更流 | `mongoc_change_stream_t` |
| URI | `mongoc_uri_t`, `mongoc_uri_new()` |
| 会话 | `mongoc_client_session_t` |
| 错误 | `mongoc_error_domain_t`, `mongoc_error_code_t` |

---

## 7. libmongoc 主要源文件分类汇总

### 按功能统计

| 功能领域 | .c 文件数 | 核心文件 |
|----------|----------|---------|
| 客户端与连接 | 5 | client, client-pool, uri, handshake, init |
| 集群与拓扑 | 10 | cluster, topology, topology-description, topology-scanner, topology-background-monitoring, server-description, server-monitor, host-list, deprioritized-servers |
| CRUD 操作 | 9 | collection, database, cursor, cursor-array, cursor-cmd, cursor-find, cursor-change-stream, aggregate, find-and-modify |
| 批量操作 | 2 | bulk-operation, bulkwrite |
| GridFS | 6 | gridfs, gridfs-bucket, gridfs-file, gridfs-file-list, gridfs-file-page, 3 个 stream-gridfs 流 |
| 流与 Socket | 8 | stream, stream-buffered, stream-file, stream-socket, stream-tls, socket, server-stream, 3 个 TLS 平台实现 |
| TLS (3 后端) | 5 | secure-channel (Win), secure-transport (Mac), openssl, stream-tls-openssl, stream-tls-openssl-bio |
| 认证 (6 机制) | 12 | cluster-sasl, scram, cluster-sspi, sspi, cluster-cyrus, cyrus, cluster-aws, cluster-oidc, oidc-cache, oidc-callback, oidc-env, http |
| 加密 | 6 | crypto, crypto-cng, crypto-common-crypto, crypto-openssl, crypt, client-side-encryption |
| RPC/协议 | 6 | cmd, opcode, rpc, mcd-rpc, mcd-azure, mcd-nsinfo |
| 压缩 | 3 | compression (zlib/zstd/snappy 综合) |
| 工具/基础 | 25+ | error, log, counters, apm, retryable-cmd, timeout, init, interrupt, async, set, queue, buffer, sasl, 等等 |
| **总计** | **117** | |

---

## 8. 测试框架

### 8.1 测试类型

1. **单元测试**: CTest 注册，由 `test-libmongoc` 二进制运行（`build/cmake/LoadTests.cmake` 加载）
2. **JSON 规范测试**: 来自 MongoDB 统一测试格式的 JSON/YAML 测试文件
3. **模拟服务器测试**: `tests/mock_server/` 模拟 MongoDB 服务器
4. **SSL/TLS 测试**: 使用 `x509gen/` 生成的证书
5. **离线/在线区分**: `MONGOC_TEST_OFFLINE`, `MONGOC_TEST_SKIP_LIVE`, `MONGOC_TEST_SKIP_MOCK` 环境变量控制

### 8.2 CTest 测试注册

`LoadTests.cmake` 动态发现 `test-libmongoc` 中的所有测试用例：
- 运行 `test-libmongoc --tests-cmake --no-fork` 输出 CMake 代码
- 解析测试用例名称、标签、超时、依赖关系
- 支持 fixtures（如 `fake_kms_provider_server`）用于需要外部服务的测试

### 8.3 关键测试依赖

- **Python 3**: 运行 `bottle.py` KMS 代理, `simple_http_server.py`
- **MongoDB 实例**: `MONGOC_TEST_HOST`, `MONGOC_TEST_PORT` 环境变量
- **认证凭据**: `MONGOC_TEST_USER`, `MONGOC_TEST_PASSWORD`
- **SSL 证书**: `MONGOC_TEST_SSL_PEM_FILE`, `MONGOC_TEST_SSL_CA_FILE`

---

## 9. 构建流程概要

```
CMake 配置
  ├── BuildVersion.cmake    # 读取 VERSION_CURRENT → BUILD_VERSION
  ├── ParseVersion.cmake    # 解析版本号为 MAJOR.MINOR.MICRO[-PRE]
  ├── LoadVersion.cmake     # 加载版本到 CMake 变量
  ├── MongoSettings.cmake   # 声明所有构建选项（mongo_setting/mongo_bool_setting）
  ├── MongoPlatform.cmake   # 创建 mongo::detail::c_platform 平台抽象目标
  ├── GeneratePkgConfig.cmake # 提供 mongo_generate_pkg_config()
  └── VerifyHeaders.cmake   # CMake 3.24+ 头文件验证
  │
  ├── 检测: Threads, C++编译器, librt, libm, ws2_32
  ├── add_subdirectory(src/common)       # common 优先构建
  ├── add_subdirectory(src/libbson)      # libbson（符号: _bson_mcommon_*）
  └── add_subdirectory(src/libmongoc)    # libmongoc（符号: _mongoc_mcommon_*）
      ├── TLS 后端检测（SecureChannel/SecureTransport/OpenSSL）
      ├── SASL 后端检测（SSPI/Cyrus）
      ├── 压缩库检测（zlib 内嵌/zstd 系统/snappy 系统）
      ├── utf8proc 检测（内嵌/系统）
      ├── KMS 消息库集成（kms-message 子项目）
      └── 测试/示例/文档 构建

CMake 构建
  ├── cmake --build . --config RelWithDebInfo
  └── cmake --install . --prefix <path>
```

---

## 10. 关键设计特点

1. **同名符号隔离**: common 库通过 `MCOMMON_NAME_PREFIX` 编译宏分别导出为 `_bson_mcommon_*` 和 `_mongoc_mcommon_*`，允许 libbson 和 libmongoc 各自内嵌一份 common 代码而无符号冲突。

2. **平台抽象层**: `mongo::detail::c_platform` 接口库为目标提供跨平台编译和链接选项。

3. **后端可插拔**: TLS (3 后端)、SASL (2 后端)、压缩 (3 算法)、加密 (3 后端) 均通过运行时 + 编译时双重抽象实现。

4. **声明式构建配置**: `mongo_setting()` / `mongo_bool_setting()` 宏提供带开发者模式、验证、条件可视化的声明式设置系统。

5. **现代化容器构建**: Earthly 的 `Earthfile` 提供跨 4 种 Linux 发行版的容器化构建、测试、SBOM 生成、Snyk 扫描和发布打包。
