# P2-21: Per-Server Max Connection Limit

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-21-connection-limit.md`

## Summary

Added `max_connections_` to `TCPServer` with thread-safe `std::atomic<uint32_t>`
counter. New connections are rejected with WARN log when `connection_count_`
reaches the limit. Counter increments on accept and decrements on close.
Default limit is 10000.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/tcp_server.h` | Added `max_connections_`, `connection_count_` (std::atomic<uint32_t>), `SetMaxConnections()`, `max_connections()`, `connection_count()` |
| `src/runtime/evpp/tcp_server.cc` | Accept path: reject + WARN log when `connection_count_ >= max_connections_`. Increment on accept, decrement on close via `RemoveConnection()`. |

### New Files

| File | Description |
|------|-------------|
| `src/tests/integration/network/test_connection_limit.cpp` | 3 integration tests: max connection enforcement, connection count decrement on disconnect, default limit of 10000 |

## Design

```
TCPServer::HandleAccept():
  if (connection_count_ >= max_connections_) {
    LOG_WARN("connection limit reached");
    close(new_fd);
    return;
  }
  connection_count_++;
  // ... create TCPConn, attach to loop ...

TCPServer::RemoveConnection():
  connection_count_--;
  // ... cleanup ...
```

### Test Design Notes

All 3 tests use `thread_num=0` (no worker threads) so connection handling,
cleanup, and event loop control all execute on the same thread. This avoids
cross-thread cleanup deadlocks where `HandleClose` runs on a worker thread but
`RemoveConnection` posts back to the listening loop.

## Acceptance Criteria

- [x] `SetMaxConnections()` API on `TCPServer`
- [x] Thread-safe connection count via `std::atomic<uint32_t>`
- [x] Rejection with WARN log when limit reached
- [x] Count decrements on disconnect (verified by integration test)
- [x] Default max is 10000
- [x] 3 integration tests pass
- [x] Build succeeds with no new warnings
