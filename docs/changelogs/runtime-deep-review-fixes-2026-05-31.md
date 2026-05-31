# Runtime Deep Review Fixes - 2026-05-31

## Context

Implemented the actionable fixes from `docs/analysis/runtime/runtime_deep_review_2026-05-31.md` for runtime reliability, observability, lifecycle safety, and authentication hardening.

## Runtime Reliability

- Added `ServerConfig::db_required` so deployments can explicitly choose whether database unavailability is fatal.
- Engine startup now fails fast when `db_required=true` and MongoDB configuration, URI validation, or database service initialization fails.
- Admin readiness now treats a disabled optional database as ready instead of reporting a false unhealthy state.
- Engine cleanup unregisters the config reload callback, preventing stale callbacks from surviving repeated init/cleanup cycles.
- `ConfigManager::active_environment_` is now atomic, removing a cross-thread data race between environment updates and readers.

## Observability

- Engine startup registers built-in runtime metrics automatically.
- TCP accept/close paths now maintain `evpp_connections_total` and `evpp_connections_active`.
- TCP read/write paths now update message counters and observe payload sizes in `evpp_message_size_bytes`.
- Timer dispatch now increments `evpp_timers_fired_total`.
- Database request queueing and drops now update `db_requests_total` and `db_requests_dropped_total`.

## Authentication And Security

- Session IDs now use OS cryptographic randomness through `GenerateSecureSessionId`.
- Token and JWT authentication now share the secure session ID generator.
- JWT verification now fails closed when the secret is empty.
- JWT signature comparison now uses constant-time comparison.
- JWT base64url signature generation now omits padding to match standard token encoding.

## Lifecycle And Threading

- `EventLoop::Stop` and `TCPServer::Stop` are now idempotent, making repeated shutdown paths safe.
- Cross-thread HTTP service shutdown now uses a bounded wait and logs a timeout instead of blocking indefinitely.
- Script hot reload dispatch now tracks a generation and stopped state so queued reload work is skipped after stop/restart.
- `ScriptReloader::Stop` drains queued loop work when called off the loop thread, reducing stale callback execution during teardown.
- Removed the unused `IsAllowedDoStringSource` helper from `vm.cc`, which advertised a security policy that was not actually enforced.

## Tests

- Added authentication tests for secure session ID format/uniqueness and empty-secret JWT rejection.
- Added config tests for `server.db_required` JSON loading and Lua `config.get` binding.
- Hardened built-in metrics tests to assert counter deltas instead of relying on singleton initial state.

## Verification

- `cmake --build artifacts\build --config Debug`
- `ctest --test-dir artifacts\build -C Debug --output-on-failure`
- Result: 52/52 tests passed.
