# P0-2: Message Framing Protocol — LengthPrefixedCodec

## Objective

Implement a `LengthPrefixedCodec` in C++ that transparently handles TCP message framing (packet boundaries), integrated into all protocol bindings (TCP/KCP/UDP). This eliminates the current `NextAllString()` approach that passes raw byte blobs to Lua with no message boundary awareness.

## Current State

`Buffer::NextAllString()` reads **all** data from the buffer, ignoring message boundaries:

```
Network data → libevent → TCPConn::HandleRead()
  → input_buffer_.ReadFromFD(fd)
  → msg_fn_(shared_from_this(), &input_buffer_)
    → buf->NextAllString()  ← returns all data, no framing
    → CallInstMethodStr(L, conn_ref, "on_message", data)
```

`NextAllString()` is called at 3 locations:
| Location | Protocol |
|----------|----------|
| `net_tcp_server_bind.cc:499` | TCP Server |
| `net_tcp_client_bind.cc:173` | TCP Client |
| `tcp_conn.cc:108` | TCP Conn SendStringInLoop |

Consequences:
- TCP sticky packets: Lua receives concatenated messages
- TCP fragmented packets: Lua receives partial messages
- Tests pass only on localhost (no real network conditions)

## Buffer Prerequisites (Must Fix First)

Two pre-existing Buffer issues must be addressed:

1. **`buffer.h:121`** — `Reserve()` has a stale `// TODO add the implementation logic here` comment but actually calls `grow()` — functionally correct. The TODO comment should be removed and an optimization audit done (the current grow-then-copy may be replaceable with a more efficient realloc pattern).
2. **`buffer.h:141`** — Byte order: `AppendInt16`/`AppendInt32` already use `htons`/`htonl` correctly. Only `AppendInt64`/`PrependInt64` use a custom `evppbswap_64` macro that should be replaced with `htonll` for consistency.

## Root Cause

No framing layer exists between raw network bytes and Lua message dispatch. This should be infrastructure provided once for all protocols, not reimplemented per game.

## Impact

Without framing:
- Every business module must implement its own framing (incompatible, bug-prone)
- Lua-side framing is 10-100x slower than C++
- Tests are misleading (localhost-only, never expose real TCP behavior)

## Implementation Steps

### Step 1: Fix Buffer Prerequisites

**File**: `src/runtime/evpp/buffer.h`

**Fix 1 — Remove stale TODO, audit Reserve()** (line ~122):
`Reserve()` already calls `grow()` and is functionally correct. Remove the stale `// TODO add the implementation logic here` comment. Optionally optimize the grow strategy (currently calls `grow()` which allocates, copies, and frees — could use `realloc` for potential in-place expansion).

**Fix 2 — Fix int64 byte order** (line ~141):
`AppendInt16`/`AppendInt32`/`PrependInt16`/`PrependInt32` already use `htons`/`htonl`/`ntohs`/`ntohl` correctly. Only `AppendInt64`/`PrependInt64` use a custom `evppbswap_64` macro — replace with standard `htonll`/`ntohll` for consistency.

### Step 2: Implement LengthPrefixedCodec

**File**: `src/runtime/network/length_prefixed_codec.h`

```cpp
/* Length-prefixed message codec.
 * Wire format: [4-byte big-endian length] [message body]
 * Max message size is configurable for DoS prevention. */
class LengthPrefixedCodec {
public:
    explicit LengthPrefixedCodec(uint32_t max_message_size = 64 * 1024);

    /* Encode a message: prepend 4-byte length header */
    std::string Encode(const std::string& payload);
    void Encode(const std::string& payload, Buffer* output);

    /* Decode: attempt to extract complete messages from buffer.
     * Returns extracted messages. Incomplete data stays in buffer. */
    std::vector<std::string> Decode(Buffer* buffer);

    /* Set max allowed message size (0 = unlimited, NOT recommended) */
    void SetMaxMessageSize(uint32_t max_size);

private:
    uint32_t max_message_size_;
};
```

**File**: `src/runtime/network/length_prefixed_codec.cc`

```cpp
std::string LengthPrefixedCodec::Encode(const std::string& payload) {
    if (payload.size() > max_message_size_) {
        return {};  /* or throw */
    }
    std::string result;
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    result.append(reinterpret_cast<const char*>(&len), 4);
    result.append(payload);
    return result;
}

std::vector<std::string> LengthPrefixedCodec::Decode(Buffer* buffer) {
    std::vector<std::string> messages;
    size_t readable = buffer->ReadableBytes();

    while (readable >= 4) {
        /* Peek the 4-byte length header without consuming */
        uint32_t msg_len_net = 0;
        memcpy(&msg_len_net, buffer->ReadableData(), 4);
        uint32_t msg_len = ntohl(msg_len_net);

        if (msg_len > max_message_size_) {
            /* Malformed or malicious — discard entire buffer */
            buffer->Reset();
            break;
        }

        if (readable < 4 + msg_len) {
            /* Incomplete message — wait for more data */
            break;
        }

        /* Extract complete message */
        buffer->Skip(4);  /* consume length header */
        std::string msg(buffer->ReadableData(), msg_len);
        buffer->Skip(msg_len);
        messages.push_back(std::move(msg));
        readable = buffer->ReadableBytes();
    }

    return messages;
}
```

### Step 3: Integrate Codec into TCP Server Binding

**File**: `src/runtime/script/net_tcp_server_bind.cc`

Replace the current `buf->NextAllString()` pattern:

```cpp
/* Before (line ~499): */
std::string data = buf->NextAllString();
CallInstMethodStr(L, conn_ref, "on_message", data);

/* After: */
auto messages = codec_->Decode(buf);
for (auto& msg : messages) {
    CallInstMethodStr(L, conn_ref, "on_message", msg);
}
```

Add a `LengthPrefixedCodec` member to `ServerCtx`. Initialize with configurable max size.

### Step 4: Integrate Codec into TCP Client Binding

**File**: `src/runtime/script/net_tcp_client_bind.cc`

Same pattern as Step 3. Add codec to `ClientCtx`.

### Step 5: Integrate Codec into KCP and UDP Bindings

**Files**: `src/runtime/script/net_kcp_server_bind.cc`, `src/runtime/script/net_udp_server_bind.cc`

KCP and UDP have message boundaries at the transport level (each KCP/UDP packet is one message), so the codec is optional. Make framing configurable per protocol.

### Step 6: Add `send()` Encoding

All `conn:send(data)` calls should encode through the codec:

```cpp
/* Before: */
ctx->conn->Send(data, len);

/* After: */
std::string framed = ctx->codec->Encode(std::string(data, len));
ctx->conn->Send(framed.data(), framed.size());
```

Make encoding optional (configurable) so existing clients can migrate gradually.

### Step 7: Add Configuration

**File**: `resources/config/public_config.json` and `dev_config.json`

```json
{
  "network": {
    "framing": {
      "enabled": true,
      "max_message_size": 65536
    }
  }
}
```

### Step 8: Tests

**File**: `src/tests/unit/length_prefixed_codec_test.cc`

- Encode/decode round-trip for various message sizes
- Decode with sticky packets (multiple messages in one buffer)
- Decode with fragmented packets (partial message)
- Max message size enforcement
- Empty message handling
- Large message (>64KB) rejection
- Buffer with exactly one complete message
- Buffer with trailing incomplete header

**File**: `src/tests/lua/framing_test.lua`

- Integration test: client sends multiple messages rapidly, server receives them as separate `on_message` calls
- Test with messages split across multiple TCP segments (use socket options to force small sends)

## Acceptance Criteria

1. `Buffer::Reserve()` stale TODO comment removed; Reserve is functionally correct
2. Buffer int64 byte order uses standard `htonll`/`ntohll` (16/32-bit already correct)
3. `LengthPrefixedCodec` correctly splits sticky packets
4. `LengthPrefixedCodec` correctly reassembles fragmented packets
5. Max message size is enforced (oversized messages rejected)
6. All 4 protocol bindings (TCP server/client, KCP server, UDP server) support codec
7. `send()` auto-encodes when framing is enabled
8. Framing can be enabled/disabled via config
9. Existing Lua tests continue to pass (backward compatible)
10. New integration tests pass with real TCP fragmentation

## Dependencies

- None (but must fix Buffer::Reserve and byte order first)

## Estimated Effort

- Buffer fixes: ~20 lines
- LengthPrefixedCodec: ~150 lines header + ~200 lines impl
- Binding integration (4 files): ~80 lines
- Config: ~10 lines
- Tests: ~150 lines C++ + ~100 lines Lua
- **Total**: ~490 lines

## Risks

- **Backward compatibility**: Existing Lua code using raw `on_message` data will break. Mitigation: make framing opt-in via config, default off initially, then default on in next major version.
- **Performance**: Decode loop allocates `std::string` per message. For high-throughput scenarios, consider a zero-copy variant using `std::string_view`.
- **KCP/UDP framing**: These protocols have native message boundaries. Adding length prefix is redundant but provides consistency. Make it optional for these protocols.
