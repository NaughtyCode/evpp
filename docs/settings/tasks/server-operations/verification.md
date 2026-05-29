# Verification Checklist: Server Operations

## Unit Tests

- [ ] Shutdown timeout: force-exits after `shutdown_timeout_sec`
- [ ] Shutdown timeout: clean shutdown within limit completes normally
- [ ] PID file created on startup with correct PID
- [ ] PID file removed on clean shutdown
- [ ] Second instance detects existing PID file lock → exits with error
- [ ] Exit code 2: config file not found
- [ ] Exit code 3: config parse error (invalid JSON)
- [ ] Exit code 4: config validation error (semantic)
- [ ] Exit code 5: file permission denied
- [ ] `max_connections` from config overrides hardcoded default
- [ ] TCP keepalive socket options applied from config
- [ ] Instance identity appears in log output
- [ ] ResourceLimits from config override compile-time defaults

## Integration Tests

- [ ] Start server → check PID file exists → SIGTERM → PID file removed
- [ ] Start two instances with same config → second fails with multi-instance error
- [ ] Set `shutdown_timeout_sec=1` → simulate hung physics → force-exit after 1s

## Manual Verification

- [ ] `cat server.pid` → matches `pgrep server`
- [ ] `./server --nonexistent-config` → exit code 2
- [ ] `kill -TERM <pid>` → graceful shutdown with connection drain log
