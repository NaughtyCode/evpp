# P3-12: Connector Retry/Reconnection Logic

## Objective

Implement the reconnection logic marked TODO in `connector.cc:132`.

## Current State

`connector.cc:132` has a TODO for reconnection logic — the connector does not retry failed connections. If a client's initial connection attempt fails (server temporarily down, network blip), the Connector gives up immediately.

## Implementation Steps

### Step 1: Add Retry Configuration

**File**: `src/runtime/evpp/connector.h`

```cpp
struct ConnectorConfig {
    int max_retries = 5;            /* -1 for infinite */
    int retry_interval_ms = 1000;   /* initial delay */
    int max_retry_interval_ms = 30000;  /* max backoff */
    double backoff_multiplier = 2.0;    /* exponential backoff */
};

class Connector {
public:
    void SetRetryConfig(const ConnectorConfig& config);

private:
    ConnectorConfig retry_config_;
    int retry_count_ = 0;
    int current_interval_ms_ = 0;

    void ScheduleRetry();
};
```

### Step 2: Implement Exponential Backoff

**File**: `src/runtime/evpp/connector.cc`

```cpp
void Connector::OnConnectFailed() {
    if (retry_config_.max_retries >= 0 && retry_count_ >= retry_config_.max_retries) {
        ENGINE_LOG_ERROR("Connector: max retries ({}) reached, giving up",
                         retry_config_.max_retries);
        if (error_callback_) error_callback_("Max retries reached");
        return;
    }

    retry_count_++;
    current_interval_ms_ = current_interval_ms_ == 0
        ? retry_config_.retry_interval_ms
        : std::min(
              static_cast<int>(current_interval_ms_ * retry_config_.backoff_multiplier),
              retry_config_.max_retry_interval_ms);

    ENGINE_LOG_INFO("Connector: retry {}/{} in {}ms",
                    retry_count_, retry_config_.max_retries, current_interval_ms_);

    ScheduleRetry();
}

void Connector::ScheduleRetry() {
    auto timer = loop_->RunAfter(current_interval_ms_, [this]() {
        Start();  /* re-attempt connection */
    });
}

void Connector::OnConnected() {
    /* Reset retry state on successful connection */
    retry_count_ = 0;
    current_interval_ms_ = 0;
    if (connect_callback_) connect_callback_();
}
```

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

- Failed connection → retry after interval → succeed on 3rd attempt
- Exponential backoff: delays increase correctly
- Max retries reached: error callback invoked
- Successful connection resets retry counter
- Infinite retries mode (max_retries = -1)

## Acceptance Criteria

1. Connector retries failed connections with configurable max attempts
2. Exponential backoff increases delay between retries
3. Max retry interval caps the backoff
4. Successful connection resets retry state
5. `on_retry` callback notifies Lua of retry attempts
6. Tests verify retry, backoff, max retries, and reset

## Dependencies: None | Estimated Effort: ~100 lines
