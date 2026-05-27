# P2-20: Connection-Level Encryption (Non-HTTP Paths)

## Objective

Add TLS/SSL support to TCP server and client connections, not just HTTP.

## Current State

Only HTTP connections have SSL support (via the HTTP server's built-in SSL configuration). TCP, KCP, and UDP protocols transmit data in plaintext. Any game traffic (player positions, chat messages, authentication tokens) is vulnerable to eavesdropping and tampering.

## Implementation Steps

### Step 1: Add SSL Context to TCP Server

**File**: `src/runtime/evpp/tcp_server.h`

```cpp
class TCPServer {
public:
    /* Enable SSL on this server */
    bool EnableSSL(const std::string& cert_path, const std::string& key_path);

private:
    std::unique_ptr<SSLContext> ssl_ctx_;
};
```

### Step 2: Integrate SSL into TCPConn

**File**: `src/runtime/evpp/tcp_conn.cc`

Wrap `bufferevent` with OpenSSL's `bufferevent_openssl_filter_new()` to add an SSL filter:

```cpp
/* After creating bufferevent: */
if (ssl_ctx_) {
    auto* ssl_bev = bufferevent_openssl_filter_new(
        base_, bev, ssl_ctx_->GetSSL(),
        BUFFEREVENT_SSL_ACCEPTING,  /* server side */
        BEV_OPT_CLOSE_ON_FREE);
    bev = ssl_bev;
}
```

### Step 3: Add Config

```json
{
  "network": {
    "ssl": {
      "enabled": false,
      "cert_file": "resources/certs/server.crt",
      "key_file": "resources/certs/server.key",
      "ca_file": "resources/certs/ca.crt",
      "verify_client": false
    }
  }
}
```

### Step 4: Per-Connection SSL Control

Allow enabling/disabling SSL per connection (or per listen port):

```lua
local server = net.server.listen("0.0.0.0", 443, {
    ssl = {
        cert_file = "server.crt",
        key_file = "server.key"
    }
})
```

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/tls_connection_test.cc`:
- SSL handshake: client connects with TLS, handshake completes, connection established
- Data transfer: encrypted data sent on one end, decrypted correctly on receiving end
- Certificate validation: invalid/self-signed cert → connection rejected with clear error
- Non-SSL client connecting to SSL-enabled port → graceful rejection

**Performance tests** — `src/tests/performance/tls_benchmark.cc`:
- Throughput with SSL vs without SSL (baseline comparison)

```
src/tests/unit/tls_connection_test.cc      # ~70 lines
src/tests/performance/tls_benchmark.cc     # ~30 lines
```

## Acceptance Criteria

1. TCP server supports SSL via OpenSSL (certificate + private key)
2. TCP client supports SSL (optionally verify server certificate)
3. SSL is configurable per listen port / per connection
4. Non-SSL connections to SSL-enabled ports are gracefully rejected
5. SSL connections work transparently — same `on_message` / `send` API

## Dependencies: None | Estimated Effort: ~300 lines
