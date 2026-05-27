# P0-5: Message/Payload Size Limits — DoS Prevention

## Objective

Add maximum size checks to all data input paths (network send/recv, msgpack encode, HTTP POST body) to prevent memory exhaustion DoS attacks. Make limits configurable per subsystem.

## Current State

All C++ bindings accept data of any length from `luaL_checklstring`:

```cpp
/* net_tcp_server_bind.cc — conn:send(data) */
size_t len = 0;
const char* data = luaL_checklstring(L, 2, &len);
ctx->conn->Send(data, len);  /* len has no upper bound */

/* net_tcp_client_bind.cc — client:send(data) */
size_t len = 0;
const char* data = luaL_checklstring(L, 2, &len);
conn->Send(data, len);       /* len has no upper bound */
```

Same pattern in UDP bindings, KCP bindings, HTTP POST body, and msgpack encoding. Network-side `Buffer` has no total capacity limit — `ReadFromFD` appends data indefinitely.

An attacker can execute:
```lua
while true do
    conn:send(string.rep("A", 1024 * 1024 * 1024))  -- 1GB allocation
end
```

## Root Cause

No defensive coding for resource limits. The code assumes trusted inputs in a prototype environment.

## Impact

- **Memory exhaustion**: Single malicious or buggy Lua script can allocate gigabytes
- **DoS vector**: Without message framing (P0-2), the attack surface is even larger — attacker controls TCP segment boundaries
- **No recovery**: OOM kills the entire process, taking all online players offline

## Implementation Steps

### Step 1: Define Default Limits

**File**: `src/runtime/config/limits.h`

```cpp
/* Default resource limits for untrusted input */
struct ResourceLimits {
    /* Maximum size of a single network message (bytes) */
    static constexpr uint32_t kDefaultMaxMessageSize = 64 * 1024;       /* 64 KB */

    /* Maximum size of a single msgpack-encoded payload (bytes) */
    static constexpr uint32_t kDefaultMaxMsgpackPayload = 1024 * 1024;  /* 1 MB */

    /* Maximum HTTP POST body size (bytes) */
    static constexpr uint32_t kDefaultMaxHttpBodySize = 10 * 1024 * 1024; /* 10 MB */

    /* Maximum total Buffer capacity per connection (bytes) */
    static constexpr uint32_t kDefaultMaxBufferCapacity = 256 * 1024;    /* 256 KB */

    /* Maximum number of concurrent connections */
    static constexpr uint32_t kDefaultMaxConnections = 10000;

    /* Maximum msgpack encode depth (nesting level) */
    static constexpr uint32_t kDefaultMaxMsgpackDepth = 64;
};
```

### Step 2: Add Max Message Size to Network Send

**File**: `src/runtime/script/net_tcp_server_bind.cc`

```cpp
/* In l_conn_send(): */
size_t len = 0;
const char* data = luaL_checklstring(L, 2, &len);

if (len > ctx->max_message_size) {
    return luaL_error(L, "message size %zu exceeds limit %u", len, ctx->max_message_size);
}

ctx->conn->Send(data, len);
```

Apply same check to:
- `net_tcp_client_bind.cc` — `l_client_send`
- `net_udp_server_bind.cc` — `l_udp_send`
- `net_udp_client_bind.cc` — `l_udp_client_send`
- `net_kcp_server_bind.cc` — `l_kcp_send`
- `net_kcp_client_bind.cc` — `l_kcp_client_send`

### Step 3: Add Max Buffer Capacity to Buffer

**File**: `src/runtime/evpp/buffer.h`

```cpp
class Buffer {
public:
    void SetMaxCapacity(size_t max) { max_capacity_ = max; }
    size_t GetMaxCapacity() const { return max_capacity_; }

    /* ... existing members ... */
private:
    size_t max_capacity_ = 256 * 1024;  /* default 256KB */

    /* In ReadFromFD, after reading: check capacity and refuse further reads */
};
```

**File**: `src/runtime/evpp/buffer.cc`

In `ReadFromFD()`, after appending data, check if `ReadableBytes() > max_capacity_`. If so, set an error flag on the connection and stop reading.

### Step 4: Add HTTP POST Body Limit

**File**: `src/runtime/script/net_http_bind.cc`

```cpp
/* In HTTP request handler, after reading body: */
size_t len = 0;
const char* body = luaL_checklstring(L, 3, &len);

if (len > max_http_body_size_) {
    return luaL_error(L, "HTTP body size %zu exceeds limit %u", len, max_http_body_size_);
}
```

### Step 5: Add msgpack Encode Limits

**File**: `src/runtime/script/msgpack_bind.cc`

In `l_msgpack_pack`, add:
- Maximum payload size check (total encoded bytes)
- Maximum nesting depth check (to prevent stack overflow from deeply nested tables)

### Step 6: Add Server-Level Connection Limit

**File**: `src/runtime/script/net_tcp_server_bind.cc`

```cpp
/* In l_net_server_listen: accept max_connections config */
if (server_ctx->connection_count >= server_ctx->max_connections) {
    /* Reject new connection, return error to client, close fd */
}
```

### Step 7: Configuration

**File**: `resources/config/public_config.json`

```json
{
  "network": {
    "limits": {
      "max_message_size": 65536,
      "max_buffer_capacity": 262144,
      "max_connections": 10000,
      "max_http_body_size": 10485760
    }
  },
  "msgpack": {
    "limits": {
      "max_payload_size": 1048576,
      "max_nesting_depth": 64
    }
  }
}
```

### Step 8: Tests

**File**: `src/tests/unit/message_limits_test.cc`

- Send message exceeding limit → luaL_error raised
- Buffer capacity exceeds limit → connection closed
- HTTP POST body exceeds limit → error returned
- msgpack table too deep → error returned
- Connection limit enforced → new connections rejected

## Acceptance Criteria

1. All `conn:send()` calls enforce max_message_size (default 64KB)
2. `Buffer` enforces max_capacity per connection (default 256KB)
3. HTTP POST body enforces max_http_body_size (default 10MB)
4. msgpack encode enforces max_payload_size and max_nesting_depth
5. Server enforces max_connections limit
6. All limits are configurable via JSON config
7. Limit violations produce clear error messages (not silent truncation)
8. Tests verify each limit

## Dependencies

- P0-2 (Message Framing) — Buffer capacity limit interacts with framing codec

## Estimated Effort

- Header/limits definition: ~30 lines
- 6 bind files × ~5 lines each: ~30 lines
- Buffer capacity check: ~15 lines
- msgpack depth check: ~20 lines
- Connection limit: ~15 lines
- Config schema: ~15 lines
- Tests: ~80 lines
- **Total**: ~200 lines

## Risks

- **Backward compatibility**: Existing Lua code that sends large messages (>64KB) will break. Mitigation: default limit can be raised via config; 64KB is reasonable for most game messages; larger payloads should use chunked transfer or file transfer.
- **Buffer capacity interaction**: When framing is enabled (P0-2), max_buffer_capacity must be at least `max_message_size + 4` (for length header). Add assertion at init time.
