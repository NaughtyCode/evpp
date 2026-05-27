# P2-22: DNS Resolver shared_ptr Leak Fix

## Objective

Fix the potential memory leak in `dns_resolver.cc:190` where a heap-allocated `shared_ptr` is passed to a C callback that may never trigger.

## Current State

`dns_resolver.cc:189-190` stores a heap-allocated `shared_ptr<DNSResolver>` as a raw `void*` for the C callback:

```cpp
std::shared_ptr<DNSResolver> p = shared_from_this();
evdns_cb_arg_ = new std::shared_ptr<DNSResolver>(p);
// passed as void* arg to evdns_getaddrinfo
```

The code already handles cleanup on all paths:
- **Success**: `OnResolved` static callback deletes the arg (line 288)
- **Timeout**: `OnTimeout()` deletes `evdns_cb_arg_` (line 145-147)
- **Cancel**: `Cancel()` deletes `evdns_cb_arg_` (line 112-114)  
- **Cancel via timer**: `OnCanceled()` deletes `evdns_cb_arg_` (line 165-167)
- **Request creation failure**: immediately deletes (line 202-203)

Double-delete is prevented by setting `(*pp)->evdns_cb_arg_ = nullptr` (line 286) before invoking callbacks that may trigger Cancel/OnTimeout.

**Remaining concern**: The raw `new`/`delete` + `void*` cast pattern is fragile. A future refactor could easily introduce a leak or double-free. The plan is to replace this with a safer RAII wrapper rather than fixing an active leak.

## Implementation Steps

### Step 1: Replace raw new/delete with unique_ptr + custom deleter

**File**: `src/runtime/evpp/dns_resolver.cc`

Replace the manual `new shared_ptr<DNSResolver>` + `delete` pattern with a `unique_ptr` wrapper that ensures cleanup:

```cpp
/* In AsyncDNSResolve(), replace lines 189-190: */
auto resolver_holder = std::make_unique<std::shared_ptr<DNSResolver>>(
    std::make_shared<DNSResolver>(shared_from_this()));
evdns_cb_arg_ = resolver_holder.get();

dns_req_ = evdns_getaddrinfo(dnsbase_, host_.c_str(), nullptr,
                             &hints, &DNSResolver::OnResolved,
                             resolver_holder.release());  // transfer ownership
```

The existing cleanup in `OnResolved`/`OnTimeout`/`Cancel`/`OnCanceled` already handles deletion. This change makes ownership transfer explicit via `unique_ptr::release()` rather than raw `new`.

### Step 2: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/dns_resolver_test.cc`:
- DNS resolution succeeds → `unique_ptr` ownership transferred, callback fires, ASAN clean
- DNS resolution times out → `unique_ptr` deleted in OnTimeout, no leak, ASAN clean
- Cancel during DNS resolution → `unique_ptr` deleted in Cancel path, ASAN clean
- Verify DNS resolution behavior is unchanged (same IP returned as before fix)

```
src/tests/unit/dns_resolver_test.cc   # ~50 lines
```

## Acceptance Criteria

1. Raw `new` for `evdns_cb_arg_` replaced with `unique_ptr` ownership transfer
2. All existing cleanup paths continue to work
3. ASAN clean on all DNS resolution paths
4. No behavior change — DNS resolution functions identically

## Dependencies: None | Estimated Effort: ~30 lines
