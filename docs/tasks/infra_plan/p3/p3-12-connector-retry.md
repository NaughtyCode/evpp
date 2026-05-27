# P3-12: Connector Retry/Reconnection Logic

## Objective

Implement the reconnection logic marked TODO in `connector.cc:149`.

## Current State

`connector.cc` already has `auto_reconnect()` infrastructure (lines 257-291): on connection failure, `HandleError()` checks `owner_tcp_client_->auto_reconnect()` and schedules a retry via `loop_->RunAfter(reconnect_interval, ...)`. This covers connection-refused and timeout errors.

What's missing:
- **No exponential backoff**: uses a fixed `reconnect_interval` for every retry
- **No max retries**: retries indefinitely (no `max_retries` config)
- **No Lua callback**: Lua scripts have no visibility into retry attempts
- **TODO at line 149**: `EVUTIL_ERR_CONNECT_RETRIABLE` errors during the initial `::connect()` call are not handled — they fall through without triggering the reconnect path

## Implementation Steps

### Step 1: Add Retry Configuration

**File**: `src/runtime/evpp/connector.h`

Add a `ConnectorConfig` struct for retry parameters. The existing `auto_reconnect()` and `reconnect_interval()` on `TCPClient` provide the basic on/off switch and fixed interval; extend with backoff and max retries:

```cpp
struct ConnectorConfig {
    int max_retries = 5;               /* -1 for infinite */
    int retry_interval_ms = 1000;      /* initial delay */
    int max_retry_interval_ms = 30000; /* max backoff cap */
    double backoff_multiplier = 2.0;   /* exponential backoff factor */
};
```

### Step 2: Implement Exponential Backoff in HandleError

**File**: `src/runtime/evpp/connector.cc`

Modify the existing reconnect path in `HandleError()` (lines 273-291) to use exponential backoff with a max retry cap. Currently it uses a fixed `reconnect_interval`; replace with:

```cpp
// In HandleError(), replace the fixed-interval retry (lines 273-291):
if (do_reconnect) {
    retry_count_++;
    if (retry_config_.max_retries >= 0 && retry_count_ > retry_config_.max_retries) {
        ENGINE_LOG_ERROR("Connector: max retries ({}) reached", retry_config_.max_retries);
        conn_fn_(-1, "max retries reached");
        return;
    }

    current_interval_ms_ = current_interval_ms_ == 0
        ? retry_config_.retry_interval_ms
        : std::min(
              static_cast<int>(current_interval_ms_ * retry_config_.backoff_multiplier),
              retry_config_.max_retry_interval_ms);

    // ... close fd, then:
    reconnect_timer_ = loop_->RunAfter(current_interval_ms_,
        std::bind(&Connector::Start, shared_from_this()));
}

// Reset retry state on successful connection (in HandleWrite):
void Connector::HandleWrite() {
    // ... existing code ...
    retry_count_ = 0;
    current_interval_ms_ = 0;
    status_ = kConnected;
}
```

### Step 2b: Handle EVUTIL_ERR_CONNECT_RETRIABLE

**File**: `src/runtime/evpp/connector.cc` (line ~149)

The `// TODO how to do it` at line 149 is for `EVUTIL_ERR_CONNECT_RETRIABLE` errors during the initial `::connect()` call. These indicate a transient failure (e.g., server backlog full) where retrying is appropriate. Replace the TODO with a call to `HandleError()` so these errors also enter the retry path.

### Step 3: Lua API

```lua
local client = net.client.connect("example.com", 8080, {
    retry = {
        max_retries = 10,
        retry_interval_ms = 500,
        max_retry_interval_ms = 30000,
        backoff_multiplier = 2.0,
    },
    on_retry = function(attempt, next_delay_ms)
        print(string.format("Reconnecting (attempt %d, next in %d ms)", attempt, next_delay_ms))
    end,
})
```

### Step 4: Tests

- Failed connection → retry with exponential backoff → succeed on 3rd attempt
- Exponential backoff: delays increase by backoff_multiplier each retry
- Max retries reached: error callback invoked, no more retries
- Successful connection resets retry counter and interval
- Infinite retries mode (max_retries = -1) retries indefinitely
- `EVUTIL_ERR_CONNECT_RETRIABLE` triggers retry instead of giving up

## Acceptance Criteria

1. Exponential backoff increases delay between retries (not fixed interval)
2. Max retries cap stops reconnection after N attempts
3. Max retry interval caps the backoff ceiling
4. Successful connection resets retry state
5. `on_retry` callback notifies Lua of retry attempts
6. `EVUTIL_ERR_CONNECT_RETRIABLE` errors enter the retry path
7. Existing auto_reconnect behavior is preserved
8. Tests verify backoff, max retries, reset, and RETRIABLE handling

## Dependencies: None | Estimated Effort: ~100 lines
