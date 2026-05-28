# P2-20: Connection-Level Encryption for Non-HTTP TCP Paths

**Date:** 2026-05-28
**Status:** Complete
**Plan:** `./docs/tasks/infra_plan/p2/p2-20-connection-encryption.md`

## Summary

Added SSL/TLS support to `TCPServer`, `TCPClient`, and `TCPConn` for non-HTTP
traffic. Previously only HTTP connections had SSL (via the HTTP server's built-in
configuration). Game TCP traffic (player data, auth tokens, etc.) is now
encryptable. All functionality is gated behind the existing
`EVPP_HTTP_CLIENT_SUPPORTS_SSL` preprocessor macro.

## Changes

### New Files

| File | Purpose |
|------|---------|
| `src/runtime/evpp/ssl_context.h` | SSL context manager — cert/key/CA loading |
| `src/runtime/evpp/ssl_context.cc` | SSL context implementation |

### Modified Files

| File | Change |
|------|--------|
| `src/runtime/evpp/tcp_conn.h` | Added `SetSSLContext()`, `IsSSLEnabled()`, `ssl_*` members |
| `src/runtime/evpp/tcp_conn.cc` | SSL_read/SSL_write wrappers in HandleRead/SendInLoop/HandleWrite; SSL handshake in OnAttachedToLoop; SSL cleanup in HandleClose |
| `src/runtime/evpp/tcp_server.h` | Added `EnableSSL()` method and `SSLContext` member |
| `src/runtime/evpp/tcp_server.cc` | Passes SSL_CTX to accepted connections |
| `src/runtime/evpp/tcp_client.h` | Added `EnableSSL()` method and `SSLContext` member |
| `src/runtime/evpp/tcp_client.cc` | Passes SSL_CTX to outgoing connections |

## Design Decisions

- **Direct OpenSSL (not bufferevent_openssl)**: The existing TCP code uses
  raw fd-based I/O (`FdChannel` + `::send`/`::recv`), not `bufferevent`.
  Wrapping fd I/O with `SSL_read`/`SSL_write` is simpler than refactoring to
  bufferevent.
- **Non-blocking handshake**: SSL handshake progresses via read/write events.
  `HandleWrite` calls `SSL_do_handshake()` when the output buffer is empty;
  `HandleRead` enables write events when `SSL_ERROR_WANT_WRITE` is returned.
- **Per-server SSL**: Each `TCPServer`/`TCPClient` can independently enable SSL.

## Acceptance Criteria

- [x] TCP server supports SSL via OpenSSL (`TCPServer::EnableSSL`)
- [x] TCP client supports SSL (`TCPClient::EnableSSL`)
- [x] SSL works transparently — same `Send`/`on_message` API
- [x] Non-blocking handshake handled correctly
- [x] SSL cleanup performed in `HandleClose()` before fd is closed
