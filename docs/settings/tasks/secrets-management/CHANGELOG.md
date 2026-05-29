# Changelog: Secrets Management

## 2026-05-29 — Initial implementation

### Added
- `ConfigManager::InterpolateEnvVars()` — `${VAR}` and `${VAR:-default}` support in config values
- `admin_bind_address` field in `ServerConfig` (default `127.0.0.1`)
- `config_webhook_url` and `config_webhook_timeout_sec` fields in `ServerConfig`
- Plaintext credential detection: WARN log when MongoDB URI contains `user:password@`
- `AdminHttpServer::Start()` now accepts optional `bind_address` parameter

### Changed
- `AdminHttpServer::Start()` signature updated: added `bind_address` parameter (default `127.0.0.1`)
- `engine.cc` passes `admin_bind_address` from config to admin HTTP server
- `server.cc` startup failure now outputs structured JSON to stderr

### Security
- Admin HTTP now defaults to `127.0.0.1` binding (previously `0.0.0.0`)
- MongoDB credentials embedding detected and warned
- `${ENV_VAR}` interpolation available for all string config values

### Fixed
- P0-5: Credentials can now be injected via env vars instead of plaintext JSON
- P2-8: Admin bind address is now configurable
- P2-9: External secret injection via env var interpolation supported

### Files changed
- `src/runtime/config/config.h` — ServerConfig new fields
- `src/runtime/config/config.cc` — InterpolateEnvVars, credential detection
- `src/runtime/monitoring/admin_http.h` — updated Start() signature
- `src/runtime/monitoring/admin_http.cc` — bind_address support
- `src/runtime/engine/engine.cc` — passes bind_address from config
- `src/server/server.cc` — structured JSON startup error
