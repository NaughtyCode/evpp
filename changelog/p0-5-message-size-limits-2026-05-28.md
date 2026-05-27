# P0-5: Message/Payload Size Limits — DoS Prevention

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p0/p0-5-message-size-limits.md`

## Summary

Added maximum size checks to all network send paths and a max capacity limit
to `evpp::Buffer` to prevent memory exhaustion DoS attacks. Defined shared
limit constants in `ResourceLimits` for consistent defaults across subsystems.

## Changes

### New Files

| File | Description |
|------|-------------|
| `src/runtime/config/limits.h` | `ResourceLimits` struct with 4 DoS limit constants (64KB message, 256KB buffer, 10MB HTTP body, 64 msgpack depth) |
| `src/tests/unit/vm/test_message_limits.cpp` | 11 tests: constant validation, buffer capacity, codec size enforcement |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/buffer.h` | Added `max_capacity_` field (default 256KB), `SetMaxCapacity`/`GetMaxCapacity`/`AtMaxCapacity` accessors |
| `src/runtime/evpp/buffer.cc` | `ReadFromFD` returns 0 when `AtMaxCapacity()` — refuses to read beyond limit |
| `src/runtime/script/net_tcp_server_bind.cc` | `l_conn_send`: size check via `codec->GetMaxMessageSize()` before encode |
| `src/runtime/script/net_tcp_client_bind.cc` | `l_client_send`: size check via `codec.GetMaxMessageSize()` before encode |
| `src/runtime/script/net_udp_client_bind.cc` | `l_udp_client_send` + `l_udp_client_send_to`: size check via `kDefaultMaxMessageSize` |
| `src/runtime/script/net_kcp_client_bind.cc` | `l_kcp_client_send`: size check via `kDefaultMaxMessageSize` |
| `src/runtime/CMakeLists.txt` | Added `config/limits.h` to CONFIG_SOURCES implicitly |
| `src/tests/unit/CMakeLists.txt` | Added `test_message_limits` target |

## Limit Enforcement

| Subsystem | Limit | Enforcement Point |
|-----------|-------|-------------------|
| TCP send | 64 KB (`codec.GetMaxMessageSize()`) | Pre-check before `Encode()` — raises `luaL_error` |
| UDP/KCP send | 64 KB (`kDefaultMaxMessageSize`) | Pre-check before `Send()` — raises `luaL_error` |
| Buffer receive | 256 KB (`max_capacity_`) | `ReadFromFD()` returns 0 at capacity |
| Decode side | 64 KB (`LengthPrefixedCodec`) | Already enforced by P0-2 codec |

## Acceptance Criteria

- [x] All `send()` calls enforce max message size (64KB default) with clear error messages
- [x] Buffer enforces `max_capacity_` (256KB default) — `ReadFromFD` refuses at capacity
- [x] Shared constants in `ResourceLimits` for consistent defaults
- [x] 11 unit tests pass (15 assertions)
- [x] Existing tests pass (sandbox: 115, scriptvm: 40, lual_error: 21)
- [x] Build succeeds with no new warnings
