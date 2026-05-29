# Implementation Plan: Server Operations

## Step 1: Configurable shutdown timeout

Modify `config.h`:
- Add `shutdown_timeout_sec` to `ServerConfig` (default 30)

Modify `engine.cc`:
- In `Cleanup()`, wrap each phase with timeout check
- If total cleanup exceeds `shutdown_timeout_sec`, log CRITICAL and force-exit

## Step 2: Graceful connection draining

Modify `engine.cc` `Cleanup()`:
- Add new phase between "Network Shutdown" start and actual close:
  1. Stop accepting new connections (close listen socket)
  2. Send close notify to existing connections
  3. Wait for in-flight messages to complete (up to drain_timeout)
  4. Force-close remaining connections

## Step 3: PID file management

Create `src/runtime/core/pid_file.h/cc`:
- `WritePidFile(path)`: writes PID to file with exclusive lock
- `RemovePidFile()`: removes PID file on clean shutdown
- On startup: if PID file exists and lock held, warn or exit (multi-instance prevention)

## Step 4: Structured exit codes

Define in new header or `server.cc`:
```cpp
enum class ExitCode : int {
    Success = 0,
    GenericError = 1,
    ConfigNotFound = 2,
    ConfigParseError = 3,
    ConfigValidationError = 4,
    PermissionDenied = 5,
    PortInUse = 6,
};
```

## Step 5: Runtime-configurable ResourceLimits

Modify `limits.h` → move to `config.h`:
- Replace `static constexpr` with regular fields in `ServerConfig`
- Add `ResourceLimits resource_limits` to `ServerConfig`
- Update all consumers to read from config instead of compile-time constants

## Step 6: TCP keepalive config

Modify `tcp_server.h` and `ServerConfig`:
- Add `TcpKeepaliveConfig { idle_sec, interval_sec, count }` to `ServerConfig`
- Apply `SO_KEEPALIVE`, `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT` on accepted sockets

## Step 7: Add max_connections config

Modify `config.h`:
- Add `max_connections` to `ServerConfig` (default 10000)
- Update `tcp_server.h` to read from config instead of hardcoded value

## Step 8: Add instance identity

Modify `config.h`:
- Add `InstanceIdentity { id, region, zone, cluster }` to `ServerConfig`
- Include instance.id in all log messages and metrics

## Step 9: Update tests

In test files:
- Test: shutdown timeout enforcement
- Test: PID file creation and cleanup
- Test: multi-instance prevention
- Test: exit codes for different failure modes
- Test: resource limits override from config
