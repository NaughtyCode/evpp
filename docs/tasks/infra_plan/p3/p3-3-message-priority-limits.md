# P3-3: Message Priority Queue + Per-Connection Rate Limiting

## Objective

Add message priority levels and per-connection send rate limiting to prevent a single connection from saturating the server's outgoing bandwidth.

## Current State

All messages are sent FIFO with no priority differentiation. A connection flooding chat messages can delay critical gameplay messages (position updates, combat results). No per-connection bandwidth cap exists.

## Implementation Steps

### Step 1: Priority Queue in TCPConn

**File**: `src/runtime/evpp/tcp_conn.h`

```cpp
enum class MessagePriority {
    kCritical = 0,  /* combat results, disconnect notifications */
    kHigh = 1,      /* position updates, RPC calls */
    kNormal = 2,    /* game messages */
    kLow = 3,       /* chat, analytics */
};

class TCPConn {
public:
    void Send(const void* data, size_t len, MessagePriority priority = MessagePriority::kNormal);

private:
    /* Priority queue: ordered by (deadline, priority) */
    struct PendingMessage {
        MessagePriority priority;
        std::string data;
        int64_t enqueue_time;
        bool operator<(const PendingMessage& other) const {
            if (priority != other.priority) return priority > other.priority;
            return enqueue_time > other.enqueue_time;
        }
    };
    std::priority_queue<PendingMessage> send_queue_;
};
```

### Step 2: Per-Connection Rate Limiting

```cpp
class RateLimiter {
public:
    RateLimiter(uint32_t max_bytes_per_sec);

    /* Returns bytes allowed to send right now */
    uint32_t Consume(uint32_t requested_bytes);

private:
    uint32_t max_bytes_per_sec_;
    uint32_t tokens_;                /* current token bucket fill */
    int64_t last_refill_time_;
};
```

Apply rate limiter to each TCP connection's send path. Excess bytes are queued for next tick.

### Step 3: Global Bandwidth Manager

```cpp
class BandwidthManager {
public:
    /* Allocate bandwidth quota across all connections using weighted fair queuing */
    void DistributeQuota();
};
```

### Step 4: Lua API

```lua
conn:send(data, {priority = "high"})

conn:set_rate_limit(1024 * 100)  -- 100 KB/s per connection
```

### Step 5: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/message_priority_test.cc`:
- Priority queue: when backlogged, critical messages dequeued before high, high before normal, normal before low
- Rate limiter: tokens consumed per byte sent, refilled at configured rate
- Burst allowance: short burst above rate succeeds, sustained rate above limit is capped
- Per-connection rate limits: one connection throttled doesn't affect another
- Configurable priority levels and rate limits via JSON config

```
src/tests/unit/message_priority_test.cc   # ~80 lines
```

## Acceptance Criteria

1. Messages have 4 priority levels (Critical/High/Normal/Low)
2. Priority queue dequeues higher-priority messages first
3. Per-connection rate limiter caps bytes/second
4. Rate limits are configurable
5. Tests verify priority and rate limit behavior

## Dependencies: None | Estimated Effort: ~300 lines
