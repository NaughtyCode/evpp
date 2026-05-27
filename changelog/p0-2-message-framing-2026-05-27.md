# P0-2: Message Framing Protocol — LengthPrefixedCodec Implementation

**Date**: 2026-05-27
**Branch**: server_engine2
**Plan**: `docs/tasks/infra_plan/p0/p0-2-message-framing.md`

## Overview

Implemented a `LengthPrefixedCodec` in C++ that transparently handles TCP message framing (packet boundaries). This eliminates the previous `NextAllString()` approach that passed raw byte blobs to Lua with no message boundary awareness, causing TCP sticky/fragmented packet issues.

## Changes

### 1. Buffer Prerequisites Fix (`src/runtime/evpp/buffer.h`)

- **Remove stale TODO**: Removed `// TODO add the implementation logic here` comment from `Reserve()` (line 122). The function already calls `grow()` and is functionally correct.
- **Replace evppbswap_64 macro**: Replaced the custom byte-swap macro with `HostToNetwork64()` / `NetworkToHost64()` static helper functions that use compiler builtins (`_byteswap_uint64` on MSVC, `__builtin_bswap64` on GCC/Clang) for efficiency and portability.

### 2. LengthPrefixedCodec Implementation (new files)

- **`src/runtime/network/length_prefixed_codec.h`**: Header with class API:
  - `Encode(payload)` / `Encode(payload, buffer)` — prepend 4-byte big-endian length header
  - `Decode(buffer)` — extract complete messages, leave incomplete data in buffer
  - `SetMaxMessageSize()` / `GetMaxMessageSize()` — DoS prevention via size limits
  
- **`src/runtime/network/length_prefixed_codec.cc`**: Implementation with:
  - Wire format: [4-byte BE length][message body]
  - Sticky packet splitting: multiple complete messages extracted from one buffer
  - Fragmented packet reassembly: incomplete messages stay in buffer
  - DoS protection: oversized messages trigger buffer reset

### 3. TCP Server Binding Integration (`src/runtime/script/net_tcp_server_bind.cc`)

- Added `engine::LengthPrefixedCodec codec` to `ServerCtx` (shared by all connections)
- Added `engine::LengthPrefixedCodec* codec` pointer to `ConnCtx` (set on connection creation)
- Message callback now calls `codec->Decode(buf)` instead of `buf->NextAllString()`, iterating over extracted messages
- `l_conn_send()` now encodes data through `codec->Encode()` before sending

### 4. TCP Client Binding Integration (`src/runtime/script/net_tcp_client_bind.cc`)

- Added `engine::LengthPrefixedCodec codec` to `ClientCtx`
- Message callback now calls `codec->Decode(buf)` instead of `buf->NextAllString()`
- `l_client_send()` now encodes data through `codec->Encode()` before sending

### 5. Build System (`src/runtime/CMakeLists.txt`)

- Added `NETWORK_SOURCES` variable for the new `src/runtime/network/` directory
- Included in `SERVERENGINE_SOURCES`

### 6. Tests

- **`src/tests/unit/network/test_length_prefixed_codec.cpp`** (12 test cases):
  - Encode/decode round-trip
  - Sticky packet splitting
  - Fragmented message reassembly
  - Max message size enforcement
  - DoS oversized message buffer reset
  - Empty message handling
  - Unlimited max size
  - Dynamic max size change
  - Encode to Buffer output
  - Trailing incomplete header
  - Exactly complete message boundary
  - Multiple interleaved writes

- **`src/tests/lua/test_framing.lua`** (4 integration tests):
  - Multiple rapid sends arrive as separate `on_message` calls
  - Large message (10KB) round-trip
  - Empty message round-trip
  - Binary data round-trip

### 7. Unit Test CMakeLists.txt (`src/tests/unit/CMakeLists.txt`)

- Added `test_length_prefixed_codec` test target with `unit.network.length_prefixed_codec` CTest registration

## Resolved Known Limitations

This implementation directly addresses known limitation #4 from the test suite report:
> "TCP 多消息测试 — 因 evpp TCP 无消息帧协议，多次 Send 会合并为一次 read"

With length-prefixed framing, multiple rapid `send()` calls now correctly result in separate `on_message` calls on the receiver, regardless of TCP segmentation.

## Acceptance Criteria Status

| # | Criterion | Status |
|---|-----------|--------|
| 1 | Buffer::Reserve() stale TODO removed | Done |
| 2 | Buffer int64 byte order uses standard approach (compiler builtins) | Done |
| 3 | LengthPrefixedCodec splits sticky packets | Done |
| 4 | LengthPrefixedCodec reassembles fragmented packets | Done |
| 5 | Max message size enforced | Done |
| 6 | TCP server/client bindings support codec | Done |
| 7 | send() auto-encodes when framing is enabled | Done |
| 8 | Framing always-on (configurable in future) | Done |
| 9 | Existing Lua tests continue to pass (backward compatible with framed peers) | To verify |
| 10 | New unit/integration tests pass | To verify |

## Notes

- KCP and UDP bindings have native message boundaries; length-prefixed framing is not applied to these protocols
- The `tcp_conn.cc:128` `NextAllString()` usage is a low-level convenience method (Send Buffer content) and does not require framing
- Framing is currently always-on. Config-based enable/disable (opt-in) will be added in a future iteration per the plan's risk mitigation strategy
