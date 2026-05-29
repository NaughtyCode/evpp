# Changelog: Server Operations

## [2026-05-29] — Implemented

### Added
- `shutdown_timeout_sec` config field — Cleanup() tracks elapsed time, force-exits on timeout
- Graceful connection draining — drain_timeout wait phase before NetworkShutdown
- PID file with exclusive lock (multi-instance prevention) — via `PidFile` class
- Structured exit codes (`ExitCode` enum) — used in `server.cc` for all exit paths
- `SetKeepAlive(fd, bool, idle, interval, count)` — full 5-arg implementation (Windows/Linux/macOS)
- TCP keepalive applied from config on accepted sockets in `listener.cc`
- `max_connections` wired from `ServerConfig` to Lua `TCPServer`
- `InstanceIdentity` logged in `server.cc` startup and `engine.cc` `Init()`

### Changed
- `ResourceLimits` already struct (runtime-configurable) from earlier batches
- `ServerConfig` fields for operations already present from earlier batches

### Fixed
- P1-6: ProfilerConfig no longer hardcoded (struct-based)
- P1-7: ResourceLimits now runtime-configurable struct
- P1-9: Graceful connection draining implemented (drain timeout phase)
- P1-10: Shutdown timeout is now configurable and enforced
- P1-13: max_connections configurable, wired to TCPServer
- P2-6: PID file + multi-instance protection via exclusive file lock
- P2-7: Structured exit codes via ExitCode enum
- P3-13: Instance identity (id, region, zone, cluster) in config and logs
- P3-19: TCP keepalive configurable (idle, interval, count) per-platform
