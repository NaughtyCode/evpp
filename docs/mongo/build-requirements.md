# mongo-c-driver 2.3.0 构建详细需求

## 1. 基本构建要求

### 1.1 构建系统

| 需求 | 版本/说明 |
|------|----------|
| **CMake** | >= 3.15（推荐 >= 3.24 以支持头文件验证） |
| **生成器** | Ninja（推荐多配置模式）、Visual Studio、Makefile |
| **C 编译器** | 支持 C99（GCC、Clang、MSVC 2015+） |
| **C 标准** | C99（强制） |
| **C++ 编译器** | 可选（用于 C++ header 兼容性检测和 mlib 测试） |

### 1.2 最低工具链版本

| 编译器 | 最低版本 |
|--------|---------|
| GCC | >= 5.0 |
| Clang | >= 3.4 |
| Apple Clang | Xcode >= 8.0 |
| MSVC | Visual Studio 2015 (MSVC 19.0) |

---

## 2. 必需系统依赖

### 2.1 所有平台通用

| 库/组件 | 说明 | 备注 |
|---------|------|------|
| **Threads** | 系统线程库 | CMake `find_package(Threads REQUIRED)` |
| **数学库 (libm)** | 数学函数 | 非 Apple 平台 `-lm` |

### 2.2 Linux

| 库/组件 | 包名 (Debian/Ubuntu) | 包名 (RHEL/CentOS) | 说明 |
|---------|---------------------|--------------------|------|
| **librt** | (glibc 内建) | (glibc 内建) | 实时扩展（clock_gettime） |
| **libpthread** | (glibc 内建) | (glibc 内建) | POSIX 线程 |
| **libresolv** | libresolv-dev（或 glibc 内建） | glibc-devel | DNS 解析（SRV 记录） |

### 2.3 Windows

| 库/组件 | 说明 |
|---------|------|
| **ws2_32** (Winsock2) | 网络 Socket API，**必需** |
| **advapi32** | Windows 高级 API（注册表等），**必需** |
| **Bcrypt** | Windows 加密 API（用于 CNG 加密） |
| **ncrypt** | 下一代加密 API |
| **secur32** | 安全支持提供者接口（SSPI） |
| **crypt32** | Windows 加密 API 函数 |
| **Shlwapi** | Shell 轻量级工具 API（SASL SSPI 需要） |

#### Windows 链接库完整清单

```
# 基础（所有 Windows 构建必需）
ws2_32.lib

# mongo::detail::c_platform
advapi32.lib                       # CMakeLists.txt: target_link_libraries(INTERFACE ...)

# TLS: Secure Channel
secur32.lib                        # TLS: SecureChannel 后端
crypt32.lib                        # TLS + SASL: 加密支持
Bcrypt.lib                         # TLS: 加密原语
ncrypt.lib                         # TLS: 下一代加密 API

# SASL: SSPI
secur32.lib                        # SASL: 安全支持提供者接口
Shlwapi.lib                        # SASL: Shell 工具（仅 SSPI 场景）

# MONGODB-AWS (kms-message)
# （需要 TLS 后端的加密库）
```

### 2.4 macOS

| 库/组件 | 说明 |
|---------|------|
| **CoreFoundation** framework | TLS: Secure Transport 后端 |
| **Security** framework | TLS: Secure Transport 后端 |
| **-lm** | 无需单独链接（libSystem 包含） |
| **-lresolv** | DNS 解析 |

---

## 3. TLS/SSL 后端依赖

### 3.1 Secure Channel（Windows 默认）

**无需额外安装**。Windows SDK 内建。

启用条件：`ENABLE_SSL=WINDOWS`（或 `AUTO` + `WIN32`）。

### 3.2 Secure Transport（macOS 默认）

**无需额外安装**。macOS SDK 内建。

启用条件：`ENABLE_SSL=DARWIN`（或 `AUTO` + `APPLE`）。

### 3.3 OpenSSL（Linux/通用）

**必需**当非 Windows/macOS 平台使用 TLS 时。

| 平台 | 安装命令 |
|------|---------|
| Debian/Ubuntu | `apt install libssl-dev` |
| RHEL/CentOS | `yum install openssl-devel` |
| Alpine | `apk add openssl-dev` |
| Arch | `pacman -S openssl` |
| macOS (Homebrew) | `brew install openssl` |
| Windows (vcpkg) | `vcpkg install openssl` |

**版本要求**: 无特定最低版本，但推荐 OpenSSL 1.1.1+（TLS 1.3 支持）。

**CMake 检测**: `find_package(OpenSSL REQUIRED)`。

**相关链接库**: `OpenSSL::SSL`, `OpenSSL::Crypto`（Windows 额外 `crypt32.lib`）。

**可选标志**:
- `MONGOC_HAVE_ASN1_STRING_GET0_DATA`: 如果 OpenSSL 版本提供 `ASN1_STRING_get0_data()`（1.1.0+），则启用，替代已弃用的 `ASN1_STRING_data()`。

---

## 4. SASL 认证后端依赖

### 4.1 SSPI（Windows 默认）

**无需额外安装**。Windows SDK 内建。

链接库: `secur32.lib crypt32.lib Shlwapi.lib`。

启用条件：`ENABLE_SASL=SSPI`（或 `AUTO` + `WIN32`）。

### 4.2 Cyrus SASL（Linux/macOS 默认）

| 平台 | 安装命令 | 版本要求 |
|------|---------|---------|
| Debian/Ubuntu | `apt install libsasl2-dev` | >= 2.0 |
| RHEL/CentOS | `yum install cyrus-sasl-devel` | >= 2.0 |
| Alpine | `apk add cyrus-sasl-dev` | >= 2.0 |
| Arch | `pacman -S cyrus-sasl` | >= 2.0 |
| macOS (Homebrew) | `brew install cyrus-sasl` | >= 2.0 |

**CMake 检测**: 自定义 `FindSASL2.cmake` 搜索 `sasl/sasl.h` + `libsasl2`。

**链接库**: `SASL2::SASL2`（IMPORTED target，自动追加 `${CMAKE_DL_LIBS}`，Cyrus SASL 需要 `dlopen`）。

**可选标志**:
- `MONGOC_HAVE_SASL_CLIENT_DONE`: 如果 SASL 版本提供 `sasl_client_done()`，则启用（替代旧版 `sasl_done()`）。

**Windows 手动安装**（非 SSPI 场景）:
- 下载 Cyrus SASL for Windows: `C:/sasl`
- 头文件: `C:/sasl/include/sasl/sasl.h`
- 库文件: `C:/sasl/lib/libsasl2.lib`

---

## 5. 压缩库依赖

### 5.1 zlib（内嵌或系统）

| 选项 | 值 | 说明 |
|------|-----|------|
| `ENABLE_ZLIB=BUNDLED` | **默认** | 使用内嵌 `zlib-1.3.1`，无需外部依赖 |
| `ENABLE_ZLIB=SYSTEM` | — | 使用系统安装的 zlib |
| `ENABLE_ZLIB=OFF` | — | 禁用 zlib 压缩 |

#### 内嵌模式（BUNDLED）

**无需任何外部依赖**。直接编译以下 15 个源文件为 OBJECT 库 `zlib_obj`：

```
src/zlib-1.3.1/adler32.c    src/zlib-1.3.1/crc32.c
src/zlib-1.3.1/deflate.c    src/zlib-1.3.1/infback.c
src/zlib-1.3.1/inffast.c    src/zlib-1.3.1/inflate.c
src/zlib-1.3.1/inftrees.c   src/zlib-1.3.1/trees.c
src/zlib-1.3.1/zutil.c      src/zlib-1.3.1/compress.c
src/zlib-1.3.1/uncompr.c    src/zlib-1.3.1/gzclose.c
src/zlib-1.3.1/gzlib.c      src/zlib-1.3.1/gzread.c
src/zlib-1.3.1/gzwrite.c
```

需要生成的文件:
- `zconf.h`（从 `zconf.h.in` 或 `zconf.h.cmakein` 配置生成）
- `HAVE_UNISTD_H` 和 `HAVE_STDARG_H` 需要被检测

#### 系统模式（SYSTEM）

**CMake 检测**: `find_package(ZLIB)`。

| 平台 | 安装命令 | 版本要求 |
|------|---------|---------|
| Debian/Ubuntu | `apt install zlib1g-dev` | 任意 |
| RHEL/CentOS | `yum install zlib-devel` | 任意 |

### 5.2 zstd

| 选项 | 说明 |
|------|------|
| `ENABLE_ZSTD=ON` | 必需 |
| `ENABLE_ZSTD=AUTO` | 自动检测（**默认**），找不到则跳过 |
| `ENABLE_ZSTD=OFF` | 禁用 |

**版本要求**: >= 0.8.0（见 SERVER-43070）。

**CMake 检测**: 通过 `pkg-config` (`pkg_check_modules(ZSTD libzstd)`)。

| 平台 | 安装命令 |
|------|---------|
| Debian/Ubuntu | `apt install libzstd-dev` |
| RHEL/CentOS | `yum install libzstd-devel` |
| Alpine | `apk add zstd-dev` |

### 5.3 Snappy

| 选项 | 说明 |
|------|------|
| `ENABLE_SNAPPY=ON` | 必需（或 `SYSTEM`，同义） |
| `ENABLE_SNAPPY=AUTO` | 自动检测（**默认**），找不到则跳过 |
| `ENABLE_SNAPPY=OFF` | 禁用 |

**CMake 检测**: 自定义 `FindSnappy.cmake` 搜索 `snappy-c.h` + `libsnappy`。

| 平台 | 安装命令 |
|------|---------|
| Debian/Ubuntu | `apt install libsnappy-dev` |
| RHEL/CentOS | `yum install snappy-devel` |
| Alpine | `apk add snappy-dev` |
| Windows | 预编译包到 `C:/snappy/`（include/ + lib/） |

---

## 6. 加密相关依赖

### 6.1 客户端字段级加密 (CSFLE / In-Use Encryption)

| 选项 | 说明 |
|------|------|
| `ENABLE_CLIENT_SIDE_ENCRYPTION=ON` | 必需 |
| `ENABLE_CLIENT_SIDE_ENCRYPTION=AUTO` | 自动检测（**默认**），找不到则跳过 |
| `ENABLE_CLIENT_SIDE_ENCRYPTION=OFF` | 禁用 |

**关键依赖**: `libmongocrypt` >= 1.15.1。

- libmongocrypt 是 MongoDB 的 C 语言加密库（独立于 C driver）。
- 通过 CMake `find_package(mongocrypt)` 或 pkg-config 检测。

**安装 libmongocrypt**（以 Debian/Ubuntu 为例）：
```bash
# 从 MongoDB 官方仓库安装
apt install libmongocrypt-dev
```

### 6.2 MONGODB-AWS 认证

| 选项 | 说明 |
|------|------|
| `ENABLE_MONGODB_AWS_AUTH=ON` | 必需 |
| `ENABLE_MONGODB_AWS_AUTH=AUTO` | 自动检测（**默认**），需要 TLS 可用 |
| `ENABLE_MONGODB_AWS_AUTH=OFF` | 禁用 |

**前置条件**: TLS 后端已启用（`MONGOC_ENABLE_SSL=1`）。

**kms-message 库**: 自动从内嵌源码构建，支持：
- AWS SigV4 签名
- Azure Key Vault 请求
- GCP KMS 请求
- KMIP 协议

kms-message 自身需要的加密后端（自动选择）：
| 平台 | 加密后端 | 链接库 |
|------|---------|--------|
| Windows | CNG (BCrypt) | `bcrypt.lib crypt32.lib` |
| macOS | Common Crypto | `-framework Security -framework CoreFoundation` |
| Linux | OpenSSL (libcrypto) | `OpenSSL::SSL OpenSSL::Crypto` |

**可选测试依赖**: `libmongoc`（`kms_message` 的在线测试需要它）。

### 6.3 系统加密配置文件

| 选项 | 说明 |
|------|------|
| `ENABLE_CRYPTO_SYSTEM_PROFILE=ON` | 启用 |
| `ENABLE_CRYPTO_SYSTEM_PROFILE=OFF` | **默认**禁用 |

**限制**: 仅在 `ENABLE_SSL=OPENSSL` 时可用。

---

## 7. Unicode 处理

### 7.1 utf8proc（SCRAM-SHA-256 必需）

| 选项 | 说明 |
|------|------|
| `USE_BUNDLED_UTF8PROC=ON` | 使用内嵌 utf8proc-2.8.0（**默认**） |
| `USE_BUNDLED_UTF8PROC=OFF` | 使用系统 libutf8proc |

#### 内嵌模式

**无需外部依赖**。内嵌 `src/utf8proc-2.8.0/`（3 个源文件，包含 1.9MB Unicode 数据表）。

#### 系统模式

| 平台 | 安装命令 |
|------|---------|
| Debian/Ubuntu | `apt install libutf8proc-dev` |
| RHEL/CentOS | `yum install utf8proc-devel` |
| macOS (Homebrew) | `brew install utf8proc` |

**CMake 检测**: 通过 `pkg-config` (`pkg_check_modules(PC_UTF8PROC libutf8proc)`)。

---

## 8. DNS/SRV 记录解析

### 8.1 Windows

**无需额外安装**。使用 Windows Dnsapi.dll (`MONGOC_HAVE_DNSAPI=1`)。

链接库: `Dnsapi.lib`。

### 8.2 Linux/macOS

使用 libresolv 的以下函数之一（按优先级）：

| 函数 | 标记 | 说明 |
|------|------|------|
| `res_nsearch()` | `MONGOC_HAVE_RES_NSEARCH` | 线程安全，**首选** |
| `res_search()` | `MONGOC_HAVE_RES_SEARCH` | 线程不安全，**回退** |
| `res_ndestroy()` | `MONGOC_HAVE_RES_NDESTROY` | BSD/macOS 清理函数 |
| `res_nclose()` | `MONGOC_HAVE_RES_NCLOSE` | Linux 清理函数 |

**CMake 检测**: `build/cmake/ResSearch.cmake`。

- 先尝试 `<resolv.h>` + `-lresolv`
- 再尝试仅 `<resolv.h>` (glibc 内建)
- 如果都找不到且 `ENABLE_SRV=ON`，则报错

---

## 9. 性能计数器（可选）

### 9.1 共享内存计数器 (SHM)

| 选项 | 说明 |
|------|------|
| `ENABLE_SHM_COUNTERS=ON` | 启用（仅 Linux + ARM macOS） |
| `ENABLE_SHM_COUNTERS=OFF` | **默认**禁用 |

需求: `shm_open()`（POSIX 共享内存）。

### 9.2 RDTSCP 指令

| 选项 | 说明 |
|------|------|
| `ENABLE_RDTSCP=ON` | 启用（仅 Intel CPU） |
| `ENABLE_RDTSCP=OFF` | **默认**禁用 |

需求: x86/x86_64 Intel CPU。

### 9.3 sched_getcpu()

| 标志 | 说明 |
|------|------|
| `MONGOC_HAVE_SCHED_GETCPU` | 自动检测（Linux 内核 >= 2.6.32） |

需求: `<sched.h>` 提供 `sched_getcpu()`。

---

## 10. 可选构建功能

### 10.1 文档构建

| 选项 | 说明 |
|------|------|
| `ENABLE_MAN_PAGES=ON` | 构建 man pages |
| `ENABLE_HTML_DOCS=ON` | 构建 HTML 文档 |

**需求**:
- **Sphinx** >= 7.1.1（Python 包，CMake 通过 `FindSphinx.cmake` 搜索 `sphinx-build` 可执行文件）
- **furo** 主题 >= 2023.5.20
- **sphinx-design** >= 0.5.0
- Python >= 3.10

**安装**:
```bash
pip install "sphinx>=7.1.1,<9.0" furo sphinx-design
```

### 10.2 测试

| 选项 | 说明 |
|------|------|
| `ENABLE_TESTS=ON` | 构建所有测试 |
| `MONGO_FUZZ=ON` | 启用 libFuzzer 模糊测试（强制 `ENABLE_STATIC=ON`） |

**需求**:
- **Python 3**: 运行测试 fixture 服务器（`bottle.py`, `simple_http_server.py`）
- **MongoDB 实例**: 集成测试（通过 `MONGOC_TEST_HOST`/`MONGOC_TEST_PORT` 指定）
- **libFuzzer**: 仅模糊测试（`MONGO_FUZZ=ON`）

### 10.3 清除器

| 选项 | 说明 | 默认（devel 模式） |
|------|------|--------------------|
| `MONGO_SANITIZE=address` | AddressSanitizer | ON（Linux/macOS） |
| `MONGO_SANITIZE=undefined` | UndefinedBehaviorSanitizer | ON（Linux/macOS） |

**验证文件**（手动管理）:
- `.lsan-suppressions` — LeakSanitizer 抑制规则
- `.tsan-suppressions` — ThreadSanitizer 抑制规则
- `.ubsan-suppressions` — UndefinedBehaviorSanitizer 抑制规则
- `valgrind.suppressions` — Valgrind 抑制规则

### 10.4 其他选项

| 选项 | 说明 |
|------|------|
| `ENABLE_MAINTAINER_FLAGS=ON` | 更严格的编译警告（开发者推荐） |
| `ENABLE_TRACING=ON` | 运行时协议跟踪（日志输出网络通信和函数进出） |
| `ENABLE_COVERAGE=ON` | 代码覆盖率检测 |
| `ENABLE_DEBUG_ASSERTIONS=ON` | 运行时调试断言 |
| `ENABLE_PIC=ON` | 位置无关代码（静态库使用 PIC，非 Windows 平台默认 ON） |
| `ENABLE_UNINSTALL=ON` | 生成卸载目标 |

---

## 11. CMake 配置示例

### 11.1 最小构建（Windows，仅静态库，无 SSL）

```powershell
cmake -S src/thirdparty/mongo-c-driver -B build/mongo `
  -G "Visual Studio 17 2022" `
  -DENABLE_MONGOC=ON `
  -DENABLE_STATIC=ON `
  -DENABLE_SHARED=OFF `
  -DENABLE_SSL=OFF `
  -DENABLE_SASL=OFF `
  -DENABLE_SRV=OFF `
  -DENABLE_ZLIB=BUNDLED `
  -DENABLE_TESTS=OFF `
  -DENABLE_EXAMPLES=OFF `
  -DCMAKE_INSTALL_PREFIX=install/mongo
```

### 11.2 标准构建（Windows，SSL + SASL + 压缩）

```powershell
cmake -S src/thirdparty/mongo-c-driver -B build/mongo `
  -G "Visual Studio 17 2022" `
  -DENABLE_MONGOC=ON `
  -DENABLE_STATIC=ON `
  -DENABLE_SHARED=ON `
  -DENABLE_SSL=WINDOWS `
  -DENABLE_SASL=SSPI `
  -DENABLE_SRV=ON `
  -DENABLE_ZLIB=BUNDLED `
  -DENABLE_ZSTD=AUTO `
  -DENABLE_SNAPPY=AUTO `
  -DENABLE_CLIENT_SIDE_ENCRYPTION=AUTO `
  -DENABLE_MONGODB_AWS_AUTH=AUTO `
  -DENABLE_TESTS=OFF `
  -DCMAKE_INSTALL_PREFIX=install/mongo
```

### 11.3 完整构建（Linux，所有功能）

```bash
cmake -S src/thirdparty/mongo-c-driver -B build/mongo \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_MONGOC=ON \
  -DENABLE_STATIC=ON \
  -DENABLE_SHARED=ON \
  -DENABLE_PIC=ON \
  -DENABLE_SSL=OPENSSL \
  -DENABLE_SASL=CYRUS \
  -DENABLE_SRV=ON \
  -DENABLE_ZLIB=BUNDLED \
  -DENABLE_ZSTD=ON \
  -DENABLE_SNAPPY=ON \
  -DENABLE_CLIENT_SIDE_ENCRYPTION=ON \
  -DENABLE_MONGODB_AWS_AUTH=ON \
  -DENABLE_MAINTAINER_FLAGS=ON \
  -DENABLE_DEBUG_ASSERTIONS=ON \
  -DENABLE_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX=install/mongo
```

### 11.4 开发者构建（Linux + 清除器）

```bash
cmake -S src/thirdparty/mongo-c-driver -B build/mongo \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_MONGOC=ON \
  -DENABLE_STATIC=ON \
  -DENABLE_SHARED=ON \
  -DENABLE_SSL=OPENSSL \
  -DENABLE_SASL=CYRUS \
  -DENABLE_ZLIB=BUNDLED \
  -DMONGO_SANITIZE="address,undefined" \
  -DENABLE_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX=install/mongo
```

---

## 12. 构建产物

### 12.1 库文件

| 库 | 静态（.lib/.a） | 动态（.dll/.so/.dylib） |
|----|----------------|------------------------|
| BSON | `bson-2.0.lib` | `bson-2.0.dll` / `libbson-2.0.so` |
| MongoDB 驱动 | `mongoc-2.0.lib` | `mongoc-2.0.dll` / `libmongoc-2.0.so` |
| KMS 消息 | `kms_message.lib` | `kms_message.dll` |

### 12.2 安装的头文件

```
<prefix>/include/
  bson-2.0/           # libbson 公开头文件
    bson.h, bson_t.h, bson-iter.h, bson-json.h, ...
  mongoc-2.0/         # libmongoc 公开头文件
    mongoc.h, mongoc-client.h, mongoc-collection.h, ...
  kms_message/        # kms-message 公开头文件
    kms_message.h, kms_request.h, ...
```

### 12.3 CMake 包配置

```
<prefix>/lib/cmake/
  bson-2.0/
    bsonConfig.cmake              # libbson CMake 包
    bsonConfigVersion.cmake
    bson_static-targets.cmake     # 或 bson_shared-targets.cmake
    00-mongo-platform-targets.cmake
  mongoc-2.0/
    mongocConfig.cmake             # libmongoc CMake 包
    mongocConfigVersion.cmake
    mongoc_static-targets.cmake    # 或 mongoc_shared-targets.cmake
    mongoc-targets.cmake
    00-mongo-platform-targets.cmake
```

### 12.4 pkg-config 文件

```
<prefix>/lib/pkgconfig/
  bson-2.0.pc             # libbson 共享库
  bson-2.0-static.pc      # libbson 静态库
  mongoc-2.0.pc           # libmongoc 共享库
  mongoc-2.0-static.pc    # libmongoc 静态库
```

---

## 13. 构建时生成的文件

### 13.1 配置头文件（configure_file）

| 模板 | 生成目标 | 来源 |
|------|---------|------|
| `src/bson/config.h.in` → | `<build>/src/bson/config.h` | libbson/CMakeLists.txt |
| `src/bson/version.h.in` → | `<build>/src/bson/version.h` | libbson/CMakeLists.txt |
| `src/mongoc/mongoc-config.h.in` → | `<build>/src/mongoc/mongoc-config.h` | libmongoc/CMakeLists.txt |
| `src/common-config.h.in` → | `<build>/src/common/common-config.h` | common/CMakeLists.txt |
| `zconf.h.in` → | `<build>/src/zlib-1.3.1/zconf.h` | src/CMakeLists.txt |

### 13.2 资源文件（Windows）

| 模板 | 生成目标 |
|------|---------|
| `libbson.rc.in` → | `<build>/libbson/libbson.rc` |
| `libmongoc.rc.in` → | `<build>/libmongoc/libmongoc.rc` |

### 13.3 CMake 包模板

| 模板 | 生成目标 |
|------|---------|
| `build/cmake/packageConfigVersion.cmake.in` → | `<build>/bsonConfigVersion.cmake` |
| `build/cmake/packageConfigVersion.cmake.in` → | `<build>/mongocConfigVersion.cmake` |

---

## 14. 符号导出控制

### 14.1 符号可见性

```
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
```

- GCC/Clang: `-fvisibility=hidden`（默认隐藏所有符号）
- MSVC: 通过 `__declspec(dllexport/dllimport)` 控制
- libbson 内部头文件: `bson-prelude.h` 定义 `BSON_API` 宏
- libmongoc 内部头文件: `mongoc-prelude.h` 定义 `MONGOC_API` 宏

### 14.2 链接器版本脚本

- `build/cmake/libmongoc-hidden-symbols.map` — 隐藏符号映射
- `build/cmake/libmongoc-hidden-symbols.txt` — 隐藏符号列表

### 14.3 BSON_STATIC 宏

当使用者链接静态 libbson 时，必须定义 `BSON_STATIC` 以减少 `__declspec(dllimport)` 声明。

---

## 15. 依赖关系图

```
消费者应用
  ├── mongoc-2.0 (libmongoc)
  │   ├── bson-2.0 (libbson)
  │   │   ├── 内嵌 jsonsl (JSON 解析)
  │   │   ├── mongo::detail::c_platform (平台抽象)
  │   │   └── _bson_mcommon_* (common 库副本)
  │   ├── _mongoc_mcommon_* (common 库副本，名称不同)
  │   ├── mongo::detail::c_platform
  │   ├── mongo::detail::c_dependencies
  │   ├── mongo::detail::c_tls_backend (TLS 后端接口)
  │   │   ├── SecureChannel (Windows)
  │   │   ├── SecureTransport (macOS)
  │   │   └── OpenSSL (Linux)
  │   ├── mongo::detail::c_sasl_backend (SASL 后端接口)
  │   │   ├── SSPI (Windows)
  │   │   └── Cyrus SASL (Linux/macOS)
  │   ├── zlib (内嵌, OBJECT 库)
  │   ├── zstd (系统, pkg-config)
  │   ├── Snappy (系统, FindSnappy)
  │   ├── utf8proc (内嵌, OBJECT 库)
  │   ├── uthash (纯头文件)
  │   ├── kms-message (内嵌, 静态库)
  │   │   ├── CNG / CommonCrypto / OpenSSL (加密后端)
  │   │   └── libmongocrypt (CSFLE 时)
  │   ├── libresolv / Dnsapi (SRV 解析)
  │   └── mongo::detail::c_resolve (DNS 解析接口)
  └── 系统库
      ├── Threads::Threads
      ├── ws2_32 (Windows)
      ├── advapi32 (Windows)
      ├── bcrypt / crypt32 / ncrypt (Windows TLS)
      └── -lm / -lrt (Linux)
```

---

## 16. 快速依赖检查清单

### Windows 构建 — 最小编译环境

- [x] CMake >= 3.15（安装）
- [x] Visual Studio 2015+ / MSVC 工具链（安装）
- [x] Windows SDK（VS 自带）
- [x] 所有平台库由 Windows SDK 提供

### Linux 构建 — 所需系统包

```bash
# Debian/Ubuntu
apt install build-essential cmake ninja-build pkg-config
# TLS: OpenSSL
apt install libssl-dev
# SASL: Cyrus
apt install libsasl2-dev
# 压缩（可选）
apt install libzstd-dev libsnappy-dev
# 文档（可选）
apt install python3 python3-pip
pip install sphinx furo sphinx-design
# CSFLE（可选）
apt install libmongocrypt-dev

# RHEL/CentOS
yum install gcc gcc-c++ cmake ninja-build pkgconfig
yum install openssl-devel cyrus-sasl-devel
yum install libzstd-devel snappy-devel
yum install python3 python3-pip
```

### macOS 构建 — 所需包

```bash
# Xcode CLT（含编译器 + SDK）
xcode-select --install
# 构建工具
brew install cmake ninja pkg-config
# TLS: OpenSSL（非必需，可用 Secure Transport）
brew install openssl
# SASL（非必需，可用环境变量传 OpenSSL 路径）
brew install cyrus-sasl
# 压缩
brew install zstd snappy
# 文档
pip install "sphinx>=7.1.1,<9.0" furo sphinx-design
```
