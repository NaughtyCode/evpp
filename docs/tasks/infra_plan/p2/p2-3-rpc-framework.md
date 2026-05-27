# P2-3: RPC Framework — msgpack-based Service Communication

## Objective

Implement an RPC framework on top of the existing msgpack serialization and network transport, enabling typed service definitions and automatic stub generation for inter-server communication.

## Current State

TCP/UDP/HTTP/KCP provide raw byte transport only. No serialization protocol layer, no IDL (Interface Definition Language), no RPC stub generation. Each service must manually encode/decode messages and manage request-response matching.

## Implementation Steps

### Step 1: Define RPC Protocol

**File**: `src/runtime/rpc/rpc_protocol.h`

```
Wire format (msgpack-encoded):
  [uint32: total_length] [msgpack_map: header] [msgpack: body]

Header:
  {
    msgid: uint32,       -- unique request ID for correlation
    service: string,     -- "PlayerService"
    method: string,      -- "GetPlayerInfo"
    type: "request" | "response" | "error"
  }
```

### Step 2: Implement RPC Client

**File**: `src/runtime/rpc/rpc_client.h`

```cpp
class RpcClient {
public:
    /* Async call: sends request, returns future */
    std::future<RpcResponse> Call(const std::string& service,
                                   const std::string& method,
                                   const msgpack::object& args);

    /* Sync call: blocks until response or timeout */
    RpcResponse CallSync(const std::string& service,
                          const std::string& method,
                          const msgpack::object& args,
                          int timeout_ms = 5000);

private:
    std::atomic<uint32_t> next_msgid_{1};
    std::unordered_map<uint32_t, std::promise<RpcResponse>> pending_;
};
```

### Step 3: Implement RPC Server

**File**: `src/runtime/rpc/rpc_server.h`

```cpp
class RpcServer {
public:
    /* Register a service implementation */
    void RegisterService(const std::string& name, RpcServiceHandler handler);

    /* Handle incoming RPC request */
    void HandleRequest(const std::string& raw_data, ResponseSender sender);

private:
    std::unordered_map<std::string, RpcServiceHandler> services_;
};
```

### Step 4: Lua RPC API

**File**: `resources/script/rpc.lua`

```lua
-- Define a service
local PlayerService = rpc.service("PlayerService", {
    get_info = function(req)
        return {name = "Alice", level = 42}
    end,
    update_profile = function(req)
        -- ...
        return {ok = true}
    end,
})

-- Register service
PlayerService:register()

-- Call a remote service
local result = rpc.call("PlayerService", "get_info", {uid = 123}, {timeout_ms = 5000})

-- Async call
rpc.call_async("PlayerService", "get_info", {uid = 123}, function(ok, result)
    if ok then
        print(result.name)
    end
end)
```

### Step 5: Tests

- Client sync call → server → response
- Client async call → multiple concurrent requests
- Timeout handling
- Error propagation
- Service not found

## Acceptance Criteria

1. RpcClient supports sync and async calls with request-response matching
2. RpcServer dispatches requests to registered service handlers
3. msgpack is used for serialization
4. Request timeout is enforced
5. Lua API: `rpc.call` and `rpc.call_async`
6. Tests verify request-response, timeout, and error handling

## Dependencies

- P0-2 (Message Framing) — RPC messages must be properly framed

## Estimated Effort: ~1000 lines
