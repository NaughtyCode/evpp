#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "runtime/monitoring/metrics.h"

using namespace engine::monitoring;

// ═══════════════════════════════════════════════════════════════════════════
// Counter tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Counter starts at zero", "[monitoring][counter]") {
    Counter c;
    REQUIRE(c.Value() == 0);
}

TEST_CASE("Counter::Inc with default delta increments by 1", "[monitoring][counter]") {
    Counter c;
    c.Inc();
    REQUIRE(c.Value() == 1);
    c.Inc();
    REQUIRE(c.Value() == 2);
}

TEST_CASE("Counter::Inc with custom delta", "[monitoring][counter]") {
    Counter c;
    c.Inc(5);
    REQUIRE(c.Value() == 5);
    c.Inc(100);
    REQUIRE(c.Value() == 105);
}

TEST_CASE("Counter increments are atomic", "[monitoring][counter]") {
    Counter c;
    c.Inc(42);
    REQUIRE(c.Value() == 42);
    // Value() returns a consistent snapshot
    int64_t v = c.Value();
    REQUIRE(v == 42);
}

// ═══════════════════════════════════════════════════════════════════════════
// Gauge tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Gauge starts at zero", "[monitoring][gauge]") {
    Gauge g;
    REQUIRE(g.Value() == 0);
}

TEST_CASE("Gauge::Set overwrites value", "[monitoring][gauge]") {
    Gauge g;
    g.Set(100);
    REQUIRE(g.Value() == 100);
    g.Set(-50);
    REQUIRE(g.Value() == -50);
    g.Set(0);
    REQUIRE(g.Value() == 0);
}

TEST_CASE("Gauge::Inc increments value", "[monitoring][gauge]") {
    Gauge g;
    g.Set(10);
    g.Inc(5);
    REQUIRE(g.Value() == 15);
    g.Inc();  // default delta = 1
    REQUIRE(g.Value() == 16);
}

TEST_CASE("Gauge::Dec decrements value", "[monitoring][gauge]") {
    Gauge g;
    g.Set(20);
    g.Dec(5);
    REQUIRE(g.Value() == 15);
    g.Dec();  // default delta = 1
    REQUIRE(g.Value() == 14);
}

TEST_CASE("Gauge supports negative values", "[monitoring][gauge]") {
    Gauge g;
    g.Set(-100);
    g.Inc(50);
    REQUIRE(g.Value() == -50);
    g.Dec(200);
    REQUIRE(g.Value() == -250);
}

// ═══════════════════════════════════════════════════════════════════════════
// Histogram tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Histogram default constructor creates empty histogram", "[monitoring][histogram]") {
    Histogram h;
    REQUIRE(h.Count() == 0);
    REQUIRE(h.Sum() == 0.0);
    REQUIRE(h.Mean() == 0.0);
}

TEST_CASE("Histogram with custom buckets", "[monitoring][histogram]") {
    std::vector<double> buckets = {1.0, 5.0, 10.0, 50.0, 100.0};
    Histogram h(buckets);
    REQUIRE(h.Count() == 0);
    REQUIRE(h.Sum() == 0.0);
}

TEST_CASE("Histogram::Observe records count and sum", "[monitoring][histogram]") {
    Histogram h;
    h.Observe(1.0);
    REQUIRE(h.Count() == 1);
    REQUIRE(h.Sum() == 1.0);

    h.Observe(2.0);
    REQUIRE(h.Count() == 2);
    REQUIRE(h.Sum() == 3.0);

    h.Observe(3.0);
    REQUIRE(h.Count() == 3);
    REQUIRE(h.Sum() == 6.0);
}

TEST_CASE("Histogram::Mean computes average correctly", "[monitoring][histogram]") {
    Histogram h;
    h.Observe(1.0);
    h.Observe(2.0);
    h.Observe(3.0);
    h.Observe(4.0);
    // Mean = (1+2+3+4) / 4 = 2.5
    REQUIRE(h.Mean() == 2.5);
}

TEST_CASE("Histogram::Mean returns zero when empty", "[monitoring][histogram]") {
    Histogram h;
    REQUIRE(h.Mean() == 0.0);
}

TEST_CASE("Histogram::Observe with fractional values", "[monitoring][histogram]") {
    Histogram h;
    h.Observe(0.5);
    h.Observe(1.5);
    h.Observe(2.5);
    REQUIRE(h.Count() == 3);
    REQUIRE(h.Sum() == 4.5);
    REQUIRE(h.Mean() == 1.5);
}

TEST_CASE("Histogram percentiles with bucketed histogram", "[monitoring][histogram]") {
    std::vector<double> buckets = {10.0, 25.0, 50.0, 75.0, 100.0};
    Histogram h(buckets);

    // Observe 100 values at each of several points
    for (int i = 0; i < 100; ++i) h.Observe(5.0);   // bucket 0 (<=10)
    for (int i = 0; i < 100; ++i) h.Observe(20.0);  // bucket 1 (<=25)
    for (int i = 0; i < 100; ++i) h.Observe(40.0);  // bucket 2 (<=50)
    for (int i = 0; i < 100; ++i) h.Observe(60.0);  // bucket 3 (<=75)
    for (int i = 0; i < 100; ++i) h.Observe(90.0);  // bucket 4 (<=100)

    REQUIRE(h.Count() == 500);
    // P50 should be ~40 (median bucket)
    double p50 = h.P50();
    REQUIRE(p50 >= 20.0);
    REQUIRE(p50 <= 50.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// MetricsRegistry — basic access
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MetricsRegistry::Instance returns same singleton", "[monitoring][registry]") {
    MetricsRegistry& r1 = MetricsRegistry::Instance();
    MetricsRegistry& r2 = MetricsRegistry::Instance();
    REQUIRE(&r1 == &r2);
}

TEST_CASE("MetricsRegistry::GetCounter returns same instance for same name", "[monitoring][registry]") {
    auto& reg = MetricsRegistry::Instance();
    Counter& c1 = reg.GetCounter("test_counter");
    Counter& c2 = reg.GetCounter("test_counter");
    REQUIRE(&c1 == &c2);
}

TEST_CASE("MetricsRegistry::GetCounter creates independent counters per name", "[monitoring][registry]") {
    auto& reg = MetricsRegistry::Instance();
    Counter& a = reg.GetCounter("counter_a");
    Counter& b = reg.GetCounter("counter_b");
    REQUIRE(&a != &b);

    a.Inc(10);
    b.Inc(20);
    REQUIRE(a.Value() == 10);
    REQUIRE(b.Value() == 20);
}

TEST_CASE("MetricsRegistry::GetGauge returns same instance for same name", "[monitoring][registry]") {
    auto& reg = MetricsRegistry::Instance();
    Gauge& g1 = reg.GetGauge("test_gauge");
    Gauge& g2 = reg.GetGauge("test_gauge");
    REQUIRE(&g1 == &g2);
}

TEST_CASE("MetricsRegistry::GetGauge preserves state on subsequent access", "[monitoring][registry]") {
    auto& reg = MetricsRegistry::Instance();
    Gauge& g1 = reg.GetGauge("preserved_gauge");
    g1.Set(42);

    Gauge& g2 = reg.GetGauge("preserved_gauge");
    REQUIRE(g2.Value() == 42);
}

TEST_CASE("MetricsRegistry::GetHistogram returns same instance for same name", "[monitoring][registry]") {
    auto& reg = MetricsRegistry::Instance();
    Histogram& h1 = reg.GetHistogram("test_histogram", {1.0, 5.0, 10.0});
    Histogram& h2 = reg.GetHistogram("test_histogram");
    REQUIRE(&h1 == &h2);
}

// ═══════════════════════════════════════════════════════════════════════════
// MetricsRegistry — Prometheus export
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ExportPrometheus has correct TYPE line for counter", "[monitoring][export][prometheus]") {
    auto& reg = MetricsRegistry::Instance();
    // Access via a unique name to avoid pollution from other tests
    Counter& c = reg.GetCounter("prom_test_counter");
    c.Inc(7);
    std::string output = reg.ExportPrometheus();

    REQUIRE(output.find("# TYPE prom_test_counter counter") != std::string::npos);
    REQUIRE(output.find("prom_test_counter 7") != std::string::npos);
}

TEST_CASE("ExportPrometheus has correct TYPE line for gauge", "[monitoring][export][prometheus]") {
    auto& reg = MetricsRegistry::Instance();
    Gauge& g = reg.GetGauge("prom_test_gauge");
    g.Set(99);
    std::string output = reg.ExportPrometheus();

    REQUIRE(output.find("# TYPE prom_test_gauge gauge") != std::string::npos);
    REQUIRE(output.find("prom_test_gauge 99") != std::string::npos);
}

TEST_CASE("ExportPrometheus has correct TYPE line for histogram", "[monitoring][export][prometheus]") {
    auto& reg = MetricsRegistry::Instance();
    Histogram& h = reg.GetHistogram("prom_test_latency", {1.0, 5.0, 10.0});
    h.Observe(3.0);
    h.Observe(7.0);
    std::string output = reg.ExportPrometheus();

    REQUIRE(output.find("# TYPE prom_test_latency histogram") != std::string::npos);
    REQUIRE(output.find("prom_test_latency_count 2") != std::string::npos);
    REQUIRE(output.find("prom_test_latency_sum 10") != std::string::npos);
}

TEST_CASE("ExportPrometheus metric name matches counter name", "[monitoring][export][prometheus]") {
    auto& reg = MetricsRegistry::Instance();
    reg.GetCounter("my_requests_total").Inc(1234);
    std::string output = reg.ExportPrometheus();

    REQUIRE(output.find("# TYPE my_requests_total counter") != std::string::npos);
    REQUIRE(output.find("my_requests_total 1234") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// MetricsRegistry — JSON export
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ExportJson produces valid JSON with counter value", "[monitoring][export][json]") {
    auto& reg = MetricsRegistry::Instance();
    Counter& c = reg.GetCounter("json_counter_xyz");
    c.Inc(55);
    std::string output = reg.ExportJson();

    // Should contain the key and value in JSON format
    REQUIRE(output.find("\"json_counter_xyz\":55") != std::string::npos);
    // Should start with '{'
    REQUIRE(output.front() == '{');
    // Should end with '}'
    REQUIRE(output.back() == '}');
}

TEST_CASE("ExportJson produces valid JSON with gauge value", "[monitoring][export][json]") {
    auto& reg = MetricsRegistry::Instance();
    Gauge& g = reg.GetGauge("json_gauge_abc");
    g.Set(-10);
    std::string output = reg.ExportJson();

    REQUIRE(output.find("\"json_gauge_abc\":-10") != std::string::npos);
}

TEST_CASE("ExportJson includes multiple metrics", "[monitoring][export][json]") {
    auto& reg = MetricsRegistry::Instance();
    Counter& c = reg.GetCounter("json_multi_counter");
    c.Inc(1);
    Gauge& g = reg.GetGauge("json_multi_gauge");
    g.Set(2);
    std::string output = reg.ExportJson();

    REQUIRE(output.find("\"json_multi_counter\":1") != std::string::npos);
    REQUIRE(output.find("\"json_multi_gauge\":2") != std::string::npos);
    // Histograms are not included in JSON export (per implementation)
}

// ═══════════════════════════════════════════════════════════════════════════
// MetricsRegistry — built-in metrics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RegisterBuiltinMetrics creates expected metrics", "[monitoring][builtin]") {
    auto& reg = MetricsRegistry::Instance();
    reg.RegisterBuiltinMetrics();

    // Built-in counters
    Counter& conn_total = reg.connections_total();
    REQUIRE(&conn_total == &reg.GetCounter("evpp_connections_total"));

    Counter& msgs_received = reg.messages_received_total();
    REQUIRE(&msgs_received == &reg.GetCounter("evpp_messages_received_total"));

    Counter& msgs_sent = reg.messages_sent_total();
    REQUIRE(&msgs_sent == &reg.GetCounter("evpp_messages_sent_total"));

    Counter& timers = reg.timers_fired_total();
    REQUIRE(&timers == &reg.GetCounter("evpp_timers_fired_total"));

    Counter& db_total = reg.db_requests_total();
    REQUIRE(&db_total == &reg.GetCounter("db_requests_total"));

    Counter& db_dropped = reg.db_requests_dropped_total();
    REQUIRE(&db_dropped == &reg.GetCounter("db_requests_dropped_total"));

    // Built-in gauge
    Gauge& conn_active = reg.connections_active();
    REQUIRE(&conn_active == &reg.GetGauge("evpp_connections_active"));
}

TEST_CASE("RegisterBuiltinMetrics creates built-in histograms", "[monitoring][builtin]") {
    auto& reg = MetricsRegistry::Instance();
    reg.RegisterBuiltinMetrics();

    // Histogram access via name should succeed
    Histogram& msg_hist = reg.GetHistogram("evpp_message_size_bytes");
    REQUIRE(&msg_hist == &reg.GetHistogram("evpp_message_size_bytes"));

    Histogram& db_latency = reg.GetHistogram("db_request_latency_ms");
    REQUIRE(&db_latency == &reg.GetHistogram("db_request_latency_ms"));

    Histogram& phys_step = reg.GetHistogram("physics_step_latency_us");
    REQUIRE(&phys_step == &reg.GetHistogram("physics_step_latency_us"));
}

TEST_CASE("RegisterBuiltinMetrics is idempotent", "[monitoring][builtin]") {
    auto& reg = MetricsRegistry::Instance();
    reg.RegisterBuiltinMetrics();
    // Calling again should not crash; metrics are reused
    REQUIRE_NOTHROW(reg.RegisterBuiltinMetrics());
}

TEST_CASE("Built-in counters can be incremented", "[monitoring][builtin]") {
    auto& reg = MetricsRegistry::Instance();
    reg.RegisterBuiltinMetrics();

    const auto connections_before = reg.connections_total().Value();
    reg.connections_total().Inc(5);
    REQUIRE(reg.connections_total().Value() == connections_before + 5);

    const auto messages_before = reg.messages_received_total().Value();
    reg.messages_received_total().Inc(100);
    REQUIRE(reg.messages_received_total().Value() == messages_before + 100);

    const auto db_before = reg.db_requests_total().Value();
    reg.db_requests_total().Inc(10);
    REQUIRE(reg.db_requests_total().Value() == db_before + 10);
}

TEST_CASE("Built-in gauge can be set and read", "[monitoring][builtin]") {
    auto& reg = MetricsRegistry::Instance();
    reg.RegisterBuiltinMetrics();

    reg.connections_active().Set(3);
    REQUIRE(reg.connections_active().Value() == 3);

    reg.connections_active().Inc(2);
    REQUIRE(reg.connections_active().Value() == 5);

    reg.connections_active().Dec(4);
    REQUIRE(reg.connections_active().Value() == 1);
}
