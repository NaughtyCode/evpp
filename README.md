# CloudEngine

**CloudEngine is a universal game server infrastructure** — the C++ layer implements the foundation (networking, timers, database, physics), while the Lua scripting layer defines and executes business logic.

## Architecture

```
External Clients (Unity / Unreal / Custom C++)
        │
        ▼
┌──────────────────────────────┐
│  GameClient C API            │  Pure C, ABI-safe surface
│  (src/client/)               │  TCP, UDP, KCP, HTTP, Timer, Log
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│  RUNTIME ENGINE              │  src/runtime/
│                              │
│  ┌────────────────────────┐  │
│  │ ENGINE CORE            │  │  Singleton: Engine, ConfigManager,
│  │ Init / Start / Run     │  │  TimerManager (HrTimer, TimerWheel,
│  │ Tick / Shutdown        │  │  AlarmTimer, ClockManager)
│  └───────────┬────────────┘  │
│              │               │
│  ┌───────────▼────────────┐  │
│  │ LUA VM LAYER           │  │  ScriptVM, ScriptImporter
│  │ Lua ↔ C++ bindings:    │  │  Modules: net (TCP/UDP/KCP/HTTP),
│  │   net, timer, log,     │  │  timer, log, msgpack, import
│  │   msgpack, import      │  │
│  └───────────┬────────────┘  │
│              │               │
│  ┌───────────▼────────────┐  │
│  │ NETWORK LAYER (evpp)   │  │  EventLoop (libevent-based)
│  │ TCP, HTTP, UDP, KCP    │  │  Client + Server for each protocol
│  └───────────┬────────────┘  │
│              │               │
│  ┌───────────▼────────────┐  │
│  │ OPTIONAL SUBSYSTEMS    │  │
│  │ Physics (JoltPhysics)  │  │  Profiler (Perfetto tracing)
│  └────────────────────────┘  │
└──────────────────────────────┘
```

## Startup Flow

```
main()
  ├─ WinSockGuard (WSAStartup)
  ├─ ConfigManager::Load("resources/config")
  │    ├─ engine.json  → EngineConfig { log, frame, scripts_dir }
  │    └─ server.json  → ServerConfig { http, msgpack }
  ├─ Engine::Init()
  │    ├─ InitLogger()              → Quill async logger
  │    ├─ ProfilerManager::Init()   → Perfetto tracing
  │    ├─ TimerManager init
  │    ├─ EventLoop creation        → libevent event_base
  │    ├─ ScriptVM creation         → luaL_newstate() + luaL_openlibs()
  │    ├─ PhysicsEngineBridge::Init()
  │    ├─ ScriptVM::DoDirectory("resources/script")
  │    └─ ScriptVM::InitScript()    → calls global Lua OnInit()
  └─ Engine::Run()
       ├─ Engine::Start()           → frame timer + signal watchers
       └─ loop_->Run()              → blocks until Shutdown()
```

## Per-Frame Loop

```
frame_timer fires
  └─ Engine::FrameLoop()
       ├─ TimerManager::update()
       ├─ PhysicsEngineBridge::Tick()    (if enabled)
       └─ ScriptVM::UpdateScript()       → calls global Lua OnTick()
```

## Key Dependencies

| Layer | Library | Purpose |
|-------|---------|---------|
| Network | libevent | Event loop, async I/O |
| Scripting | Lua 5.4+ (onelua.c) | Business logic VM |
| Logging | Quill | Async structured logging |
| Serialization | glaze | Compile-time JSON reflection |
| Concurrency | concurrentqueue | Lock-free MPMC queue |
| Physics | JoltPhysics v5.5.1 | Rigid body physics simulation |
| Reliability | KCP (ikcp.c) | Reliable UDP transport |
| Profiling | Perfetto | System-wide tracing |

## Memory Allocation

All dynamic memory allocation and deallocation must go through the `MEM_*` wrapper macros defined in `src/runtime/core/mem/mem.h`. Raw `new`, `delete`, `delete[]`, `malloc`, `free`, `calloc`, and `realloc` are forbidden in production runtime code.

| Macro | Replaces | Usage |
|---|---|---|
| `MEM_NEW(T, ...)` | `new T(...)` | Single-object heap allocation |
| `MEM_DELETE(ptr)` | `delete ptr` | Single-object deallocation |
| `MEM_NEW_ARR(T, n)` | `new T[n]` | Array allocation |
| `MEM_DELETE_ARR(ptr)` | `delete[] ptr` | Array deallocation |
| `MEM_MALLOC(size)` | `malloc(size)` | C-style uninitialized allocation |
| `MEM_FREE(ptr)` | `free(ptr)` | C-style deallocation |
| `MEM_CALLOC(n, size)` | `calloc(n, size)` | C-style zero-initialized allocation |
| `MEM_REALLOC(ptr, size)` | `realloc(ptr, size)` | C-style reallocation |
| `MEM_NEW_NOTHROW(T, ...)` | `new (std::nothrow) T(...)` | Non-throwing allocation (returns nullptr on failure) |
| `MEM_NEW_ARR_NOTHROW(T, n)` | `new (std::nothrow) T[n]` | Non-throwing array allocation |

**What must NOT be wrapped**: Third-party library free/destroy functions (`SSL_CTX_free`, `bson_free`, `event_base_free`, `mongoc_*_destroy`, etc.) — these are external API calls, not raw memory deallocation.

**Smart pointers**: `std::make_unique<T>(...)` and `std::make_shared<T>(...)` remain the preferred idiomatic pattern and do not need wrapping. When constructing a smart pointer from a raw allocation, use `MEM_NEW`: `std::unique_ptr<T> ptr(MEM_NEW(T, args...))`.

## Project Structure

```
src/
├─ server/          Standalone server entry point (main.cc)
├─ runtime/         CloudEngine.dll — core engine library
│   ├─ engine/      Engine singleton (Init, Start, Run, Tick, Shutdown)
│   ├─ config/      JSON-based configuration manager
│   ├─ core/        Logging (Quill) and timer systems
│   ├─ vm/          Lua VM wrapper, script importer
│   ├─ script/      Lua ↔ C++ bindings (net, timer, log, msgpack, import)
│   ├─ evpp/        Inline networking library (TCP, HTTP, UDP, KCP)
│   ├─ physics/     JoltPhysics integration
│   └─ profiler/    Perfetto tracing integration
├─ client/          GameClient.dll — C ABI for external game engines
│   └─ include/     client.h — 900+ line C API surface
├─ thirdparty/      Third-party dependencies
└─ tests/           Unit and smoke tests
resources/
├─ config/          engine.json, server.json
├─ script/          Lua business logic scripts
└─ physics/         Physics assets and material configs
```
