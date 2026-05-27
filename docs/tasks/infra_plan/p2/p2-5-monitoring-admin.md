# P2-5: Monitoring Metrics + Admin HTTP Endpoint

## Objective

Add production-ready metrics collection and an HTTP admin endpoint (`/health`, `/stats`, `/metrics`) for operational monitoring.

## Current State

No production metrics exist. Perfetto traces are suitable for development profiling but not for continuous production monitoring. The three diagnostic channels (Quill logs, fprintf, std::cout) provide no structured metrics.

## Implementation Steps

### Step 1: Define Metrics System

**File**: `src/runtime/monitoring/metrics.h`

```cpp
/* Counter — monotonically increasing (e.g., total_connections) */
class Counter {
public:
    void Inc(int64_t delta = 1);
    int64_t Value() const;
private:
    std::atomic<int64_t> value_{0};
};

/* Gauge — point-in-time value (e.g., active_connections, memory_usage) */
class Gauge {
public:
    void Set(int64_t value);
    int64_t Value() const;
private:
    std::atomic<int64_t> value_{0};
};

/* Histogram — distribution (e.g., message_latency_ms, request_size_bytes) */
class Histogram {
public:
    void Observe(double value);
    double Mean() const;
    double P50() const;
    double P95() const;
    double P99() const;
    /* ... */
};

/* MetricsRegistry — central collection of all metrics */
class MetricsRegistry {
public:
    static MetricsRegistry& Instance();

    Counter& GetCounter(const std::string& name, const std::string& help = "");
    Gauge& GetGauge(const std::string& name, const std::string& help = "");
    Histogram& GetHistogram(const std::string& name, const std::string& help = "",
                             const std::vector<double>& buckets = {});

    /* Export all metrics in Prometheus text format */
    std::string ExportPrometheus() const;

    /* Export as JSON */
    std::string ExportJson() const;
};
```

### Step 2: Add Built-in Metrics

**File**: `src/runtime/engine/engine_metrics.cc`

Key metrics to instrument:
- `evpp_connections_active` (gauge)
- `evpp_connections_total` (counter)
- `evpp_messages_received_total` (counter)
- `evpp_messages_sent_total` (counter)
- `evpp_message_size_bytes` (histogram)
- `evpp_timers_active` (gauge)
- `evpp_timers_fired_total` (counter)
- `db_requests_total` (counter)
- `db_requests_dropped_total` (counter)
- `db_request_latency_ms` (histogram)
- `lua_vm_memory_kb` (gauge)
- `lua_gc_count_total` (counter)
- `engine_uptime_seconds` (counter)
- `physics_bodies_active` (gauge)
- `physics_step_latency_us` (histogram)

### Step 3: Admin HTTP Endpoint

**File**: `src/runtime/monitoring/admin_server.h`

```cpp
class AdminServer {
public:
    void Start(uint16_t port);

    /* Endpoints: */
    /* GET /health — returns 200 if server is healthy */
    /* GET /stats  — returns JSON with key metrics */
    /* GET /metrics — returns Prometheus text format */
    /* POST /admin/reload-config — triggers config reload */
    /* POST /admin/gc — triggers Lua GC */
    /* GET /admin/connections — lists active connections */

private:
    evpp::HTTPServer server_;
};
```

### Step 4: Tests

After completing each step, add automated tests in the following categorized locations:

**Unit tests** — `src/tests/unit/metrics_test.cc`:
- Counter: increment from multiple threads, verify final count is correct
- Gauge: set, increment, decrement, verify value at each step
- Histogram: record samples, verify p50/p95/p99 are accurate within tolerance
- Prometheus format: export counter/gauge/histogram, verify valid Prometheus text format

**Unit tests** — `src/tests/unit/admin_server_test.cc`:
- Health endpoint returns 200 with JSON body
- Stats endpoint returns engine metrics (connection count, message rate)
- Config reload action triggers `ConfigManager::Reload()`
- GC trigger action invokes `lua_gc`

```
src/tests/unit/metrics_test.cc        # ~70 lines
src/tests/unit/admin_server_test.cc   # ~60 lines
```

## Acceptance Criteria

1. Counter, Gauge, Histogram metric types implemented
2. Built-in metrics for network, timer, database, Lua VM, physics, engine
3. Admin HTTP server on configurable port
4. `/health`, `/stats`, `/metrics` endpoints functional
5. Prometheus text format export
6. Admin actions: config reload, GC trigger, connection list

## Dependencies

- None (independent)

## Estimated Effort: ~400 lines
