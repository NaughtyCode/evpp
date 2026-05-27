# P1-6: Database Backpressure Notification

## Objective

Implement backpressure notification for the database request queue so that when the SPSC queue is full, the caller receives clear feedback (not silent discard) and the system can degrade gracefully.

## Current State

`DBThread::EnqueueRequest` (`db_thread.cc:170-187`) already has basic backpressure:
- Checks `request_queue_.size_approx() >= config_.thread_pool.request_queue_size` before enqueue
- Logs WARN when queue is full ("DBThread[N]: request queue full")
- Returns `false` to the caller

`DatabaseService::SendRequest` returns `bool`, and the Lua binding (`db_service_main_bind.cc`) passes this to Lua as a boolean return value — but Lua scripts may not check it:

```cpp
/* db_service_main_bind.cc — existing pattern */
bool ok = DatabaseService::Instance().SendRequest(std::move(req));
lua_pushboolean(L, ok ? 1 : 0);  /* returns to Lua — but Lua side may not check */
return 1;
```

What's missing:
- **No synthetic response**: When the queue is full, the caller gets `false` but no `DbResponse` is generated — the request_id is lost and no callback fires
- **No metrics**: No tracking of dropped/enqueued/completed/error counts
- **No degradation path**: No retry mechanism or backoff strategy for callers
- **Per-thread pending counts**: No visibility into which DBThread is overloaded

## Root Cause

The SPSC queue's fixed capacity (default ~128 items for moodycamel) is not treated as a monitored resource. Round-robin dispatch is uniform but can't handle hotspots — some DBThreads may be busier than others.

## Impact

1. **Silent data loss**: Lua calls `db_send_request`, receives `false`, but no indication of WHY or what to do
2. **Undebuggable inconsistencies**: Can't distinguish "request never sent" from "sent but execution failed" from "discarded by queue"
3. **No degradation path**: No backoff, retry, or fallback mechanism when queue is full

## Implementation Steps

### Step 1: Add Dropped Status to DbResponse

**File**: `src/runtime/database/db_types.h`

```cpp
enum class DbRequestStatus {
    kEnqueued,    /* Successfully placed in queue */
    kCompleted,   /* Execution completed (success or error) */
    kDropped,     /* Queue full — request discarded */
    kTimeout,     /* Request exceeded timeout */
};

struct DbResponse {
    DbRequestStatus status;
    std::string error_message;
    /* ... existing fields ... */
};
```

### Step 2: Log Warnings on Queue Full

**File**: `src/runtime/database/data_service/db_thread.cc`

```cpp
bool DBThread::EnqueueRequest(DbRequestPtr request) {
    if (!request_queue_.enqueue(std::move(request))) {
        DB_LOG_WARN("DBThread queue full — dropping request. "
                    "Queue size: ~{}, thread: {}",
                    request_queue_.size_approx(), thread_name_);
        return false;
    }
    return true;
}
```

### Step 3: Generate Dropped Response

**File**: `src/runtime/database/data_service/database_service.cc`

```cpp
bool DatabaseService::SendRequest(DbRequestPtr request) {
    auto* thread = SelectThread();  /* round-robin */

    if (!thread->EnqueueRequest(std::move(request))) {
        /* Queue full — generate a synthetic response so the caller
         * receives notification rather than waiting forever */
        auto response = std::make_shared<DbResponse>();
        response->status = DbRequestStatus::kDropped;
        response->request_id = request->request_id;
        response->error_message = "Queue full — request dropped";

        /* Dispatch response callback immediately (on calling thread) */
        if (request->callback) {
            request->callback(response);
        }
        return false;
    }
    return true;
}
```

### Step 4: Add Backpressure Metrics

**File**: `src/runtime/database/data_service/database_service.h`

```cpp
struct DatabaseMetrics {
    std::atomic<uint64_t> total_enqueued{0};
    std::atomic<uint64_t> total_dropped{0};
    std::atomic<uint64_t> total_completed{0};
    std::atomic<uint64_t> total_errors{0};

    /* Per-thread pending counts */
    std::vector<std::atomic<uint32_t>> per_thread_pending;
};
```

Update metrics on enqueue/dequeue/drop.

### Step 5: Add Lua-Side Status Handling

**File**: `src/runtime/database/data_service/db_service_main_bind.cc`

```cpp
/* Update db_send_request binding to return richer status info */
int l_db_send_request(lua_State* L) {
    /* ... existing validation ... */

    auto req = std::make_shared<DbRequest>();
    /* ... populate ... */

    /* Set up callback to deliver response to Lua */
    req->callback = [L, callback_ref](DbResponsePtr response) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
        lua_pushboolean(L, response->status == DbRequestStatus::kCompleted);
        lua_pushstring(L, response->error_message.c_str());
        /* ... push result document ... */
        Engine::Instance().GetMainLoop()->RunInLoop([L, callback_ref]() {
            /* Dispatch to Lua in main thread */
        });
    };

    bool ok = DatabaseService::Instance().SendRequest(std::move(req));
    if (!ok) {
        /* Immediate failure — callback already invoked with kDropped status */
    }

    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
```

### Step 6: Tests

**File**: `src/tests/unit/db_backpressure_test.cc`

```cpp
TEST(DBBackpressureTest, QueueFull_ReturnsDroppedResponse) {
    /* Fill queue to capacity, verify next request gets kDropped */
}

TEST(DBBackpressureTest, DroppedRequest_Logged) {
    /* Verify WARN log is emitted on queue full */
}

TEST(DBBackpressureTest, MetricsUpdated) {
    /* Verify total_dropped increments on queue full */
}
```

## Acceptance Criteria

1. `DbRequestStatus::kDropped` exists in the response type
2. Queue full condition is logged at WARN level
3. Dropped requests generate an immediate synthetic response (not silent)
4. Backpressure metrics are tracked (enqueued/dropped/completed/errors)
5. Lua side can check `db_send_request` return value for queue status
6. Tests verify dropped request handling

## Dependencies

- None (independent)

## Estimated Effort

- DbResponse status enum: ~5 lines
- DBThread warning log: ~5 lines
- DatabaseService synthetic response: ~20 lines
- Metrics: ~30 lines
- Lua binding update: ~30 lines
- Tests: ~80 lines
- **Total**: ~170 lines

## Risks

- **Synthetic response thread safety**: The dropped response callback may be invoked on the calling thread, not the main thread. Ensure the callback is thread-safe or dispatch to main loop.
- **Queue size**: moodycamel's default capacity may need tuning. Add configurable capacity per DBThread.
- **false positive WARN logs**: During legitimate load spikes, queue-full warnings are expected. Consider rate-limiting the log to avoid flooding.
