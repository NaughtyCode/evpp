# P3-3: Message Priority Queue & Per-Connection Rate Limiting

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p3/p3-3-message-priority-limits.md`

## Summary

Added token-bucket `RateLimiter` class for per-connection bandwidth control and
`MessagePriority` enum for 4-tier message ordering (critical/high/normal/low).
Priority queue drains after the output buffer is emptied in `HandleWrite`,
ensuring critical messages (heartbeats, RPC acks) are never stuck behind bulk data.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/evpp/rate_limiter.h` | Token-bucket rate limiter — `Consume(bytes)` API, nanosecond-precision refill, `SetRate()` for dynamic reconfiguration, 0 = unlimited mode |
| `src/tests/unit/network/test_rate_limiter.cpp` | 9 unit tests: unlimited mode, token consumption, depletion, time-based refill, SetRate reset, zero-byte consume, large request clamping |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/tcp_conn.h` | Added `MessagePriority` enum (kCritical=0, kHigh, kNormal, kLow), `RateLimiter` member, priority queue `pending_priority_msgs_`, `SendWithPriority()` and `SetRateLimit()` methods |
| `src/runtime/evpp/tcp_conn.cc` | `HandleWrite` drains priority queue after main buffer is flushed. `SendWithPriority` enqueues by priority tier. Rate limiter `Consume()` gates bytes-per-second on each write tick. |
| `src/tests/unit/CMakeLists.txt` | Added `test_rate_limiter` target |

## Design

### Token-Bucket Algorithm

```
Consume(requested_bytes):
  1. if max_bytes_per_sec == 0 → unlimited, return requested_bytes
  2. Refill(): tokens += elapsed_ns * max_bytes_per_sec / 1e9
  3. allowed = min(requested_bytes, tokens)
  4. tokens -= allowed
  5. return allowed
```

Tokens capped at `max_bytes_per_sec` to prevent burst accumulation. Nanosecond
precision via `std::chrono::steady_clock`.

### Priority Queue

Messages sent with `SendWithPriority()` are stored in `pending_priority_msgs_`
(flat map keyed by priority tier). On each `HandleWrite` tick:
1. Drain main output buffer first
2. Drain priority queue in tier order (critical → high → normal → low)
3. Each message passes through rate limiter — excess bytes re-queued

## Acceptance Criteria

- [x] `RateLimiter` class implemented with token-bucket algorithm
- [x] 9 unit tests pass (unlimited, consumption, depletion, refill, reset, zero-byte, clamping)
- [x] `MessagePriority` enum with 4 tiers
- [x] `SendWithPriority()` and priority queue drain in `HandleWrite`
- [x] Per-connection `SetRateLimit()` API
- [x] Build succeeds with no new warnings
