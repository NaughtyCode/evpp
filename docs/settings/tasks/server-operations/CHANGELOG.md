# Changelog: Server Operations

## [Unreleased]

### Added
- (pending) `shutdown_timeout_sec` config field
- (pending) Graceful connection draining (stop accept → drain → close)
- (pending) PID file with exclusive lock (multi-instance prevention)
- (pending) Structured exit codes (ExitCode enum)
- (pending) `TcpKeepaliveConfig` (idle_sec, interval_sec, count)
- (pending) `max_connections` config field
- (pending) `InstanceIdentity` (id, region, zone, cluster)

### Changed
- (pending) `ResourceLimits` from `static constexpr` to runtime-configurable
- (pending) TCP keepalive now applied from config, not OS defaults

### Fixed
- (pending) P1-6: ProfilerConfig no longer hardcoded
- (pending) P1-7: ResourceLimits now runtime-configurable
- (pending) P1-9: Graceful connection draining implemented
- (pending) P1-10: Shutdown timeout is now configurable
- (pending) P1-13: max_connections configurable
- (pending) P2-6: PID file + multi-instance protection
- (pending) P2-7: Structured exit codes
- (pending) P3-13: Instance identity in config
- (pending) P3-19: TCP keepalive configurable
