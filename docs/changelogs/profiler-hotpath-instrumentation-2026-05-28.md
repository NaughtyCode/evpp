# Profiler: Hot-Path Instrumentation Expansion

**Date:** 2026-05-28
**Status:** Complete

## Summary

Extended Perfetto tracing coverage to all core hot-path systems: entity lifetime,
space management, AOI spatial queries, auth/session operations, physics command
dispatching, message encoding/decoding, and VM sandbox initialization. Added 4 new
trace categories and 35 convenience macros, with full zero-cost disabled fallbacks.

## Changes

### Infrastructure

| File | Change |
|------|--------|
| `src/runtime/profiler/profiler_categories.h` | Added categories: `engine.entity`, `engine.space`, `engine.aoi`, `engine.auth` |
| `src/runtime/profiler/profiler_events.h` | Added 35 convenience macros (entity, space, AOI, auth) with both enabled and disabled forms |

### Instrumented Files

| File | Events Added |
|------|-------------|
| `src/runtime/entity/entity.cc` | Activate, Suspend, Destroy, BindConnection, UnbindConnection, AddTimer |
| `src/runtime/entity/entity_manager.cc` | CreateEntity, GetEntity, DestroyEntity, DestroyAll, FindByConnection, ForEachActive |
| `src/runtime/space/space.cc` | CreateEntity, GetEntity, DestroyEntity, OnPlayerJoin, OnPlayerLeave, Update, LoadScripts |
| `src/runtime/space/space_manager.cc` | CreateSpace, CreateSpaceWithId, GetSpace, DestroySpace, CreateDefaultSpace |
| `src/runtime/space/space_message.cc` | SendMessage, ProcessPending |
| `src/runtime/space/connection_router.cc` | RouteNewConnection, RouteMessage, RouteDisconnection |
| `src/runtime/aoi/aoi_manager.cc` | RegisterEntity, UnregisterEntity, OnEntityMove, RecomputeVisibility, QueryRadius |
| `src/runtime/aoi/spatial_index.cc` | Insert, Update, Remove, QueryRadius, QueryAOI, QueryAOIAt |
| `src/runtime/auth/auth_backend.cc` | Authenticate, ValidateSession |
| `src/runtime/auth/session_manager.cc` | CreateSession, IsSessionValid, GetSession, GetSessionById, RevokeSession, CleanupExpired |
| `src/runtime/physics/physics_system.cc` | Initialize, Start, Shutdown, Tick, EnqueueSpawn, EnqueueDestroy, FetchResult, GetTransform, UpdateScript |
| `src/runtime/physics/physics_diff.cc` | GenerateDiff, ObjectRegistry::Register, ObjectRegistry::Unregister |
| `src/runtime/script/msgpack_bind.cc` | l_msgpack_pack, l_msgpack_unpack, l_msgpack_unpack_one, l_msgpack_unpack_limit, ExportMsgPack |
| `src/runtime/script/bind_util.cc` | LuaError |
| `src/runtime/script/net_lifetime.cc` | NetAliveGuard::TryAcquire, NetAliveGuard::Release |
| `src/runtime/network/length_prefixed_codec.cc` | Encode (string), Encode (buffer), Decode |
| `src/runtime/vm/sandbox.cc` | luaL_openlibs_sandboxed |

## Design Decisions

- **Convenience macros**: System-level macros (e.g. `ENGINE_PROFILE_ENTITY_CREATE()`) centralize
  category+name pairs, reducing duplication and making it easy to rename/re-categorize events.
- **No duplicate instrumentation**: All pre-existing profiling sites (engine frame loop,
  timer manager, physics thread event loop, VM ctor/dtor, script export) were intentionally
  skipped to avoid redundant trace entries.
- **Granularity calibrated per system**: Per-entity/per-packet functions get dedicated
  events (hot, high-frequency paths); infrastructure functions (event loop, fd channel)
  were intentionally skipped to avoid trace noise.
- **Zero cost when disabled**: All new macros have `#else` fallbacks expanding to `do {} while(0)`,
  ensuring no runtime overhead when `ENGINE_PROFILER_ENABLED` is not defined.

## Statistics

- **19 files** changed (17 source + 2 infrastructure)
- **262 lines** added, 1 line removed
- **4 new** trace categories
- **35 new** convenience macros
- **~55 new** trace event sites on hot paths
