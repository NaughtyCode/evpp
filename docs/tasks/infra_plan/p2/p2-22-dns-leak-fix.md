# P2-22: DNS Resolver shared_ptr Leak Fix

## Objective

Fix the potential memory leak in `dns_resolver.cc:190` where a heap-allocated `shared_ptr` is passed to a C callback that may never trigger.

## Current State

`dns_resolver.cc:190`:

```cpp
auto* cb_data = new std::shared_ptr<DNSResolver::Callback>(std::move(callback));
/* cb_data passed to evdns_getaddrinfo as void* arg.
 * If the DNS request times out or is cancelled, the C callback
 * never fires, and cb_data is leaked. */
```

The `shared_ptr` is heap-allocated because the callback signature requires a `void*`. If `evdns_getaddrinfo` never invokes the callback (timeout, cancellation, network error), the heap allocation is permanently leaked.

## Implementation Steps

### Step 1: Add Timeout-Based Cleanup

**File**: `src/runtime/evpp/dns_resolver.cc`

```cpp
/* Instead of raw heap allocation, use a wrapper with timeout: */
struct DNSCallbackData {
    std::shared_ptr<DNSResolver::Callback> callback;
    std::shared_ptr<evpp::InvokeTimerPtr> timeout_timer;
};

/* In Resolve(): */
auto* data = new DNSCallbackData();
data->callback = std::make_shared<DNSResolver::Callback>(std::move(callback));

/* Set a timeout: if callback not invoked within 30s, clean up */
data->timeout_timer = std::make_shared<evpp::InvokeTimerPtr>();
*data->timeout_timer = loop_->RunAfter(30000, [data]() {
    ENGINE_LOG_WARN("DNS resolution timed out");
    delete data;
});

/* In the C callback: */
static void DNSCallback(int err, struct evutil_addrinfo* addr, void* arg) {
    auto* data = static_cast<DNSCallbackData*>(arg);
    /* Cancel the timeout — callback was invoked */
    data->timeout_timer->Cancel();
    /* ... invoke callback ... */
    delete data;
}
```

### Step 2: Cleanup on Shutdown

Add a list of outstanding DNS requests that are cancelled during `DNSResolver::Shutdown()`.

### Step 3: Tests

- DNS resolution succeeds within timeout: callback invoked, no leak
- DNS resolution times out: timeout callback fires, resource cleaned up
- Shutdown during pending DNS: requests cancelled, no leak
- ASAN verification: no leaks

## Acceptance Criteria

1. DNS callback data is cleaned up on timeout (30s default)
2. DNS callback data is cleaned up on shutdown
3. Successful resolution cancels the timeout timer
4. ASAN clean
5. Tests verify timeout and shutdown cleanup

## Dependencies: None | Estimated Effort: ~50 lines
