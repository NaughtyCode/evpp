# Changelog

## [Unreleased] — 2026-05-28

### Fixed — Logging unification
- Replaced 50+ `std::fprintf(stderr, ...)` calls with Quill `ENGINE_LOG_*` macros across engine.cc (15), physics_config.cc (18), config.cc (8), config_validator.cc (1), event_loop.cc (4), and inner_pre.cc (1).
- Engine bootstrap messages now flow through Quill once `InitLogger()` completes, eliminating split output between stderr and log files.
- Legitimate fprintf fallbacks retained only where Quill logger itself is unavailable (pre-InitLogger bootstrap, logger creation failure, null-logger guard).

### Fixed — std::exit elimination
- Replaced `std::exit(EXIT_FAILURE)` with `throw std::runtime_error(...)` in `Engine::GetScriptVM()` and `EventLoop` (2 locations). Destructors now run on fatal errors instead of hard process termination.

### Added — Admin HTTP endpoints
- New `AdminHttpServer` class in `src/runtime/monitoring/admin_http.h/cc`.
- Endpoints: `/health` (JSON status + uptime), `/stats` (JSON engine counters), `/metrics` (Prometheus text format via `MetricsRegistry::ExportPrometheus()`).
- Configurable via `ServerConfig::admin_port` (default 8081, 0 = disabled). Auto-started during `Engine::Init()`.

### Added — TCP SetLinger support
- Added `SetLinger()` to sockets layer (`sockets.h/cc`) and `TCPConn` class. Enables SO_LINGER control for graceful vs abortive connection close.

### Added — Listener bind retry
- `Listener::Listen()` now retries `bind()` up to 5 times with 200ms delay, and sets `SO_REUSEADDR` before first attempt. Mitigates TIME_WAIT port conflicts on restart.

### Added — HTTP status code table
- Extended `g_http_code_string` table from 4 to 18 entries covering common 2xx/3xx/4xx/5xx responses.

### Added — Big-endian byte order support
- `Buffer::HostToNetwork64()` now correctly handles big-endian platforms via `__BYTE_ORDER__` detection. Added `HostToNetwork32()` and `HostToNetwork16()` helpers.

### Added — SSL for TCP Server
- Added `EVPP_OPENSSL_ENABLED` compile definition to both client and server CMakeLists.txt.
- Updated `ssl_context.h/cc`, `tcp_server.h/cc`, `tcp_client.h/cc`, `tcp_conn.h/cc` to activate SSL support when `EVPP_OPENSSL_ENABLED` is defined, not just `EVPP_HTTP_CLIENT_SUPPORTS_SSL`. TCP server now accepts TLS-encrypted client connections.

### Added — Unit tests for previously uncovered modules
- `test_metrics.cpp` — MetricsRegistry (Counter, Gauge, Histogram, Prometheus/JSON export, built-in metrics).
- `test_aoi.cpp` — AOI system (SpatialGrid insert/remove/query, AOIManager events).
- `test_space.cpp` — Space system (Space lifecycle, SpaceManager, ConnectionRouter).
- `test_coroutine.cpp` — CoroutineScheduler (init, update, yield/resume lifecycle).
- `test_rpc.cpp` — RPC framework (protocol, client/server basics).
- `test_auth.cpp` — Auth framework (AuthBackend, SessionManager, token handling).
- `test_hotreload.cpp` — ScriptReloader (lifecycle, callback, crash-safety, multi-instance).
- `test_orm.cpp` — ORM (OrmSession, CRUD, schema, cache integration).
- `test_cache.cpp` — EntityCache (LRU eviction, statistics, edge cases, stress test).

### Changed — TODO resolution
- Resolved all 8 remaining TODO/FIXME items in non-thirdparty runtime code:
  - Implemented `SetLinger()` (tcp_conn.h)
  - Replaced `list<Slice>` suggestion with rationale comment (tcp_conn.h)
  - Removed stale "leave it to user layer close" note (tcp_conn.cc)
  - Added bind retry + SO_REUSEADDR (listener.cc)
  - Replaced `recvmmsg` TODO with platform compatibility note (udp_server.cc)
  - Added 14 HTTP status code strings (http/service.cc)
  - Replaced resource recycling TODO with explanation (http/service.cc)
  - Replaced performance compare TODO with rationale (httpc/request.cc)
