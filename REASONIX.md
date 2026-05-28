# CloudEngine — Working Knowledge

## Stack

- **Language**: C++23 (CXX_STANDARD 23, required)
- **Build**: CMake 4.0 — presets: `windows-msvc` (VS 2026) / `unix-make`
- **Test framework**: Catch2 v3.8.0 (header-only, fetched via FetchContent)
- **Scripting**: Lua 5.4+ (bundled onelua.c in `src/thirdparty/lua/`)
- **Key deps**: libevent (event loop), Quill (async logging), glaze (JSON reflection), concurrentqueue (lock-free MPMC), JoltPhysics v5.5.1, KCP (reliable UDP), Perfetto (tracing), Google Benchmark v1.9.1 (optional)

## Layout

| Path | What lives there |
|---|---|
| `src/server/` | Standalone server entry point (`server.cc` → `GameServer.exe`) |
| `src/runtime/` | Core engine library (`CloudEngine.dll/.so`) — engine, config, core/timer, network, physics, profiler, script bindings, VM, evpp |
| `src/client/` | `GameClient.dll` — C ABI for external engines (Unity/Unreal). `client.h` = pure C API |
| `src/tests/` | Catch2-based test suite: `unit/`, `smoke/`, `integration/`, `lua/`, `performance/` |
| `src/thirdparty/` | Bundled deps (gtest only for thirdparty libs, not project tests) |
| `resources/` | Config JSON (`engine.json`, `server.json`), Lua scripts, physics assets |
| `artifacts/` | Build output — `bin/<config>/`, `lib/<config>/` |
| `docs/` | Architecture docs, devlogs, analysis reports, changelogs |
| `.github/workflows/` | CI (`ci.yml`), lint (`lint.yml`), nightly, sanitizers |

## Commands

All commands run from `src/`. Tests must opt in via `-DBUILD_TESTING=ON`.

```bash
# Configure
cmake --preset windows-msvc -DBUILD_TESTING=ON -S src -B build
cmake --preset unix-make    -DBUILD_TESTING=ON -S src -B build

# Build
cmake --build build --config Release -j 4

# Run tests by label
ctest -C Release -L smoke       --output-on-failure
ctest -C Release -L unit        --output-on-failure -j 2
ctest -C Release -L integration --output-on-failure -j 1
ctest -C Release -L lua         --output-on-failure -j 1

# Benchmarks (requires -DCLOUDENGINE_BUILD_BENCHMARKS=ON)
ctest -C Release -L performance --output-on-failure -j 1

# Lint/format (CI runs these automatically)
clang-format -i --dry-run --Werror src/runtime/**/*.{cc,h}
clang-tidy -p build <source-file>                      # msvc/linux
luacheck resources/ --config resources/.luacheckrc     # lua scripts
```

## Conventions

- **Source extensions**: `.h` headers, `.cc` implementation (runtime), `.cpp` tests
- **Unit tests**: `src/tests/unit/<module>/test_<module>.cpp` — one executable per test file, linked against `Catch2::Catch2WithMain` + `CloudEngine`
- **Test labels**: CTest labels `smoke` / `unit` / `integration` / `lua` / `performance` — run via `ctest -L <label>`
- **Code style**: `clang-format` Google style modified — tabs (width 4), Attach braces, left-aligned pointers. Enforced on changed files via `scripts/pre-commit.sh`
- **Linting**: `clang-tidy` with `bugprone-*`, `performance-*`, `modernize-*`, `cppcoreguidelines-*`, `readability-*` — CI enforces; suppress with `// NOLINTNEXTLINE(...)`
- **Forbidden in runtime**: `fprintf(stderr, ...)` and `std::cout` — CI and pre-commit hook reject them
- **TODO baseline**: Count of `TODO|FIXME|HACK|XXX` in `src/` must stay ≤ 30 (CI-enforced)
- **Lua**: checked with `luacheck`; sandbox restricts globals (see `resources/.luacheckrc`)
- **Physics**: disabled by default; enable with `-DENGINE_PHYSICS_ENABLED=ON`
- **Output dir**: binaries go to `artifacts/bin/<config>/`; test working dir is the same

## Watch out for

- **CMake 4.0 required** — older versions will fail
- **Catch2 v3 fetched at configure time** — needs network on first configure. Version pinned to v3.8.0 in `src/tests/CMakeLists.txt`
- **Test working dir is `artifacts/bin/<config>/`**, not the build dir — tests expecting resource files relative to cwd must account for this
- **`src/thirdparty/` contains bundled, sometimes-patched deps** — do not edit without cross-referencing the origin. JoltPhysics, libevent, Lua, etc. live here
- **Server entry point is in `src/server/server.cc`** — not in `src/runtime/`. The runtime is a shared library the server links against
- **Client C API is ABI-stable pure C** — `client.h` is consumed by Unity/Unreal; breaking changes must be deliberate
