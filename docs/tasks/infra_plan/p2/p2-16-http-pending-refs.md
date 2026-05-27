# P2-16: HTTP Pending Refs O(1) — vector to unordered_set

## Objective

Replace the O(n) `std::find`-based pending ref removal in `HandleHttpResponse` with O(1) lookup using `std::unordered_set`.

## Current State

`net_http_bind.cc:81` — `HandleHttpResponse` uses `std::find` on a `std::vector<int>` (`g_http_pending_refs`) to locate and remove a pending reference:

```cpp
auto it = std::find(pending_refs_.begin(), pending_refs_.end(), ref);
if (it != pending_refs_.end()) {
    pending_refs_.erase(it);
}
```

Under high-concurrency HTTP workloads, this accumulates O(n²) overhead as the pending refs vector grows. Each HTTP request adds a ref (O(1) push_back), and each response removes a ref (O(n) find + O(n) erase).

## Root Cause

`std::vector` was chosen for simplicity. Under moderate concurrency (< 10 pending requests) this is fine. Under high concurrency (> 100 pending), the linear search becomes measurable.

## Implementation Steps

### Step 1: Replace vector with unordered_set

**File**: `src/runtime/script/net_http_bind.cc`

```cpp
/* Before: */
static std::vector<int> g_pending_http_refs;

/* After: */
static std::unordered_set<int> g_pending_http_refs;

/* AddRef becomes: */
g_pending_http_refs.insert(ref);

/* RemoveRef becomes: */
g_pending_http_refs.erase(ref);  /* O(1) average */
```

### Step 2: Update Shutdown Iteration

```cpp
/* Shutdown iteration — iterate over set instead of vector: */
void UnrefAllPending(lua_State* L) {
    std::lock_guard lock(g_pending_mutex);
    for (int ref : g_pending_http_refs) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    g_pending_http_refs.clear();
}
```

### Step 3: Benchmark

Run a benchmark comparing vector vs unordered_set for various pending request counts (1, 10, 100, 1000, 10000). Verify O(1) improvement at scale.

### Step 4: Tests

- Add/remove 1000 refs, verify all operations complete without error
- Shutdown with pending refs, verify all are unref'd
- Concurrent add/remove under mutex (thread safety)

## Acceptance Criteria

1. `g_pending_http_refs` uses `std::unordered_set<int>`
2. Add/remove operations are O(1) average
3. Shutdown correctly unrefs all pending refs from the set
4. All existing HTTP tests pass
5. Benchmark shows < 1ms for 1000 ref operations

## Dependencies

- P0-6 (RunInLoop Safety) — the g_net_alive + mutex pattern protects this data structure

## Estimated Effort: ~30 lines
