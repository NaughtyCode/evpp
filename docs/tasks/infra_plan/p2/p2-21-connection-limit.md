# P2-21: Connection Count Limit — Prevent FD Exhaustion

## Objective

Add configurable per-server maximum connection limits to prevent file descriptor exhaustion from malicious or accidental connection floods.

## Current State

No connection limit exists. A malicious client can open thousands of connections, exhausting the process's file descriptor limit and preventing legitimate clients from connecting. This is a simple DoS vector with no mitigation.

## Implementation Steps

### Step 1: Add Limit to TCP Server

**File**: `src/runtime/evpp/tcp_server.h`

```cpp
class TCPServer {
public:
    void SetMaxConnections(uint32_t max) { max_connections_ = max; }
    uint32_t GetConnectionCount() const { return connection_count_; }

private:
    uint32_t max_connections_ = 10000;  /* default */
    std::atomic<uint32_t> connection_count_{0};
};
```

### Step 2: Check on Accept

**File**: `src/runtime/evpp/tcp_server.cc`

```cpp
/* In the accept callback: */
if (connection_count_ >= max_connections_) {
    ENGINE_LOG_WARN("TCP server '{}': connection limit reached ({}/{}). "
                    "Rejecting new connection from {}.",
                    name_, connection_count_.load(), max_connections_,
                    remote_addr);
    evconnlistener_free(lev);
    close(fd);  /* immediately close the accepted socket */
    return;
}
connection_count_++;
```

### Step 3: Decrement on Close

```cpp
/* In the connection close callback: */
connection_count_--;
```

### Step 4: Add Config

```json
{
  "network": {
    "limits": {
      "max_connections": 10000
    }
  }
}
```

### Step 5: Lua API

```lua
local server = net.server.listen("0.0.0.0", 8080, {
    max_connections = 5000
})
print(server:connection_count())  -- current count
```

### Step 6: Tests

- Start server with max_connections=2
- Connect 3 clients: first 2 succeed, 3rd is rejected
- Disconnect 1 client: new connection now accepted
- Connection count returns to 0 after all clients disconnect

## Acceptance Criteria

1. TCP server enforces configurable max connection limit
2. Exceeding limit: new connections are immediately closed with logged warning
3. Connection count is correctly decremented on disconnect
4. Limit is configurable via JSON and Lua API
5. Tests verify limit enforcement and count accuracy

## Dependencies: None | Estimated Effort: ~100 lines
