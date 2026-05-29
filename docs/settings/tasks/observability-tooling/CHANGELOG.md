# Changelog: Observability & Tooling

## [Unreleased]

### Added
- (pending) Field-level diff logging in `Reload()` (old→new for each changed field)
- (pending) `ConfigManager::Dump()` — JSON snapshot of all running config
- (pending) `ConfigManager::DumpRuntime()` / `DumpServer()` — per-scope snapshots
- (pending) `GET /config/snapshot` HTTP endpoint
- (pending) Prometheus metrics: `evpp_config_reload_total`, `evpp_config_reload_errors_total`, `evpp_config_reload_duration_seconds`, `evpp_config_hash`
- (pending) `ConfigManager::ValidateOnly(path)` dry-run mode
- (pending) `--validate-config` CLI flag for server binary
- (pending) `evpp-config-validate` standalone CLI tool
- (pending) Config change webhook (POST after Reload)
- (pending) Structured JSON startup failure output to stderr

### Changed
- (pending) Reload log: from single "config reloaded" to detailed field-level diff
- (pending) Startup failure: from plain text stderr to structured JSON

### Fixed
- (pending) P2-1: Reload log now includes change details
- (pending) P2-2: Config snapshot/export API available
- (pending) P2-3: Dry-run validation mode available
- (pending) P2-5: Key naming documented and consistent
- (pending) P2-10: Prometheus config metrics added
- (pending) P2-11: Standalone CLI validator available
- (pending) P2-12: Config change webhook/event notification
