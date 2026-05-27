# P3-11: HTTP Graceful Shutdown Implementation

## Objective

Implement graceful shutdown for the HTTP server, addressing the TODO markers at `src/runtime/evpp/http/http_server.cc:304,323`.

## Current State

`http_server.cc:304,323` mark graceful shutdown as not implemented. The HTTP server likely stops accepting connections abruptly when `Stop()` is called, potentially dropping in-flight requests.

## Implementation Steps

### Step 1: Implement Drain Mode

**File**: `src/runtime/evpp/http/http_server.h`

```cpp
class HTTPServer {
public:
    /*
     * Initiate graceful shutdown:
     *   1. Stop accepting new connections
     *   2. Wait for in-flight requests to complete (with timeout)
     *   3. Send Connection: close header on remaining connections
     *   4. Close all connections
     */
    void GracefulShutdown(int timeout_ms = 30000);

private:
    std::atomic<bool> draining_{false};
    std::atomic<int> in_flight_requests_{0};
};
```

### Step 2: Implement Draining Logic

**File**: `src/runtime/evpp/http/http_server.cc`

```cpp
void HTTPServer::GracefulShutdown(int timeout_ms) {
    draining_ = true;
    ENGINE_LOG_INFO("HTTP server entering drain mode (timeout {}ms)", timeout_ms);

    /* Step 1: Stop the listener — no new connections */
    if (listener_) {
        evconnlistener_disable(listener_);
    }

    /* Step 2: Wait for in-flight requests to drain */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (in_flight_requests_ > 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            ENGINE_LOG_WARN("HTTP graceful shutdown timed out with {} requests still in flight",
                            in_flight_requests_.load());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    /* Step 3: Close remaining connections */
    /* ... */
    ENGINE_LOG_INFO("HTTP server shutdown complete");
}
```

### Step 3: Track In-Flight Requests

Increment `in_flight_requests_` at request start, decrement at response completion.

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/http_graceful_shutdown_test.cc`:
- Send long-running request, initiate GracefulShutdown → request completes before server stops
- GracefulShutdown with timeout: server stops after timeout even if requests still in flight
- During drain: no new connections accepted (SYN rejected or immediately closed)
- No pending requests: shutdown completes immediately (no unnecessary wait)

```
src/tests/unit/http_graceful_shutdown_test.cc   # ~60 lines
```

## Acceptance Criteria

1. `GracefulShutdown()` stops accepting new connections
2. In-flight requests complete before shutdown
3. Timeout forces shutdown if requests don't complete in time
4. Tests verify drain and timeout behavior

## Dependencies: None | Estimated Effort: ~100 lines
