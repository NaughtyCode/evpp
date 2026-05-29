# Changelog: Health Check Probes

## [Unreleased]

### Added
- (pending) `/health/startup` endpoint — returns 200 only after full initialization
- (pending) `/health/readiness` endpoint — validates all backend dependencies
- (pending) `/health/liveness` endpoint — lightweight event loop check
- (pending) `/health/phase` endpoint — exposes CleanupPhase
- (pending) `std::atomic<bool> initialized_` in Engine
- (pending) `std::atomic<CleanupPhase> current_phase_` in Engine
- (pending) Per-dependency health status in JSON responses

### Changed
- (pending) `/health` legacy endpoint now maps to liveness probe

### Fixed
- (pending) P0-6: Health check now validates backend dependencies
- (pending) P3-8: CleanupPhase now externally visible
- (pending) P3-9: Startup/Readiness/Liveness probes now differentiated
