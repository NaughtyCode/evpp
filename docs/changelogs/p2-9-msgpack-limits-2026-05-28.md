# P2-9: msgpack Encode Size/Depth Limits

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-9-msgpack-limits.md`

## Summary

Added a maximum payload size limit (1 MB default) to msgpack encoding to
prevent memory exhaustion from malicious or buggy Lua scripts encoding
arbitrarily large tables. The limit is enforced incrementally during encoding
via `EncodeBuf::overflow`, catching oversized payloads mid-encode rather than
after the full table is serialized.

## Changes

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/config/config_constants.h` | Added `kDefaultMsgpackMaxPayloadSize = 1 MB` |
| `src/runtime/config/config.h` | Added `max_payload_size` to `MsgpackConfig` |
| `src/runtime/script/msgpack_bind.cc` | Added `EncodeBuf::max_size` + `overflow` flag; `l_msgpack_pack` checks overflow after each argument |

## Design Decisions

- **Incremental enforcement**: `EncodeBuf::Append()` checks `data.size() + len > max_size`
  before each append, setting `overflow = true` if exceeded. This catches oversized
  tables at any nesting depth during recursive encode, not just at the top level.
- **Configurable**: `max_payload_size` is a `ServerConfig` field, adjustable via
  `server.json` without recompilation.
- **Existing depth limit**: `max_nesting_depth` already existed in config; this
  change adds the missing size dimension.

## Acceptance Criteria

- [x] Encoding enforces `max_payload_size` (default 1 MB)
- [x] Limit checked at every encode level (not just top-level)
- [x] `overflow` flag prevents further encoding after limit exceeded
- [x] Configurable via `server.json` `msgpack.max_payload_size`
