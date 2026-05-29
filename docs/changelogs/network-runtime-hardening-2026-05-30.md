# Runtime Network Hardening - 2026-05-30

## Summary

Audited the runtime networking implementation under `src/runtime` across TCP, UDP, KCP, HTTP client, buffers, rate limiting, and the length-prefixed codec. The pass focused on boundary handling, lifetime safety, cross-thread send behavior, protocol size limits, and build-visible portability issues.

## Fixes

- Hardened `LengthPrefixedCodec` against null buffers, oversized payload truncation, and incomplete-frame size overflow during decode.
- Fixed `evpp::Buffer` max-capacity semantics so `0` means unlimited, and bounded `ReadFromFD()` reads to the remaining configured capacity.
- Changed `Buffer` static constants to inline constexpr definitions to avoid unresolved externals in unit-test links on MSVC.
- Made `RateLimiter` reads thread-safe and clamped refill arithmetic to avoid token overflow after long idle periods.
- Moved TCP priority/rate-limited send queue mutation onto the event-loop thread, added delayed flush scheduling for rate-limited pending messages, and cleared pending state on close.
- Fixed HTTP client request lifetime management so async DNS/connect/retry/response callbacks keep the request alive until completion.
- Made HTTP URL parsing tolerant of empty or missing URI/host parts instead of assigning from null pointers.
- Hardened UDP client/server/message send paths with correct socket-address lengths, IPv6-friendly host handling, datagram size checks, invalid-socket checks, and receive buffer clamping.
- Hardened KCP client/server setup with socket cleanup on reconnect, validated KCP tuning parameters, stricter send-completion checks, and disabled-timeout semantics when timeout is configured as `0`.

## Tests

- Added null-buffer coverage for `LengthPrefixedCodec`.
- Added disabled max-capacity coverage for `evpp::Buffer`.

## Verification Notes

- Compilation reached the C/C++ compile stage for the touched runtime networking code without new compiler errors.
- Full target verification is currently blocked by the existing CMake/MSVC auto-export step failing on Lua object generation:
  `Auto build dll exports` reports `unrecognized file format` for `artifacts/build/server/CloudEngine.dir/<config>/lapi.obj`.
- This blocker appears outside the networking changes and affects both Debug and Release builds when building the requested unit-test targets.
