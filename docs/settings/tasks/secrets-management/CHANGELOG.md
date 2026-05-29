# Changelog: Secrets Management

## [Unreleased]

### Added
- (pending) `${ENV_VAR}` interpolation in all JSON config string values
- (pending) `${ENV_VAR:-default}` syntax with fallback defaults
- (pending) `admin_bind_address` field in `ServerConfig` (default `127.0.0.1`)
- (pending) `AdminTlsConfig` struct for admin HTTP TLS
- (pending) Plaintext credential detection warning

### Security
- (pending) MongoDB credentials can now be injected via environment variables
- (pending) Admin HTTP endpoint defaults to localhost-only binding
- (pending) Optional TLS for admin HTTP server

### Fixed
- (pending) P0-5: No more plaintext passwords in JSON files
- (pending) P2-8: Admin bind address is now configurable
- (pending) P2-9: External secret injection via env vars supported
