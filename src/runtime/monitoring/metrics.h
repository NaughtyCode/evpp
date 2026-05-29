#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"

namespace engine {
namespace monitoring {

// Counter — monotonically increasing metric (e.g., total_connections).
class CLOUD_ENGINE_API Counter {
public:
	void Inc(int64_t delta = 1) { value_.fetch_add(delta, std::memory_order_relaxed); }
	int64_t Value() const { return value_.load(std::memory_order_relaxed); }

private:
	std::atomic<int64_t> value_{0};
};

// Gauge — point-in-time value (e.g., active_connections, memory_usage).
class CLOUD_ENGINE_API Gauge {
public:
	void Set(int64_t value) { value_.store(value, std::memory_order_relaxed); }
	int64_t Value() const { return value_.load(std::memory_order_relaxed); }
	void Inc(int64_t delta = 1) { value_.fetch_add(delta, std::memory_order_relaxed); }
	void Dec(int64_t delta = 1) { value_.fetch_sub(delta, std::memory_order_relaxed); }

private:
	std::atomic<int64_t> value_{0};
};

// Histogram — distribution of values (e.g., latency, message sizes).
// Uses logarithmic buckets for efficient storage.
class CLOUD_ENGINE_API Histogram {
public:
	Histogram() = default;
	explicit Histogram(const std::vector<double>& buckets);

	void Observe(double value);
	double Mean() const;
	double P50() const;
	double P95() const;
	double P99() const;
	size_t Count() const { return count_.load(std::memory_order_relaxed); }
	double Sum() const { return sum_.load(std::memory_order_relaxed); }

private:
	std::vector<double> buckets_;
	std::vector<std::atomic<int64_t>> bucket_counts_;
	std::atomic<int64_t> count_{0};
	std::atomic<double> sum_{0.0};
	mutable std::mutex mutex_;  // protects quantile computation
};

// MetricsRegistry — central collection of all metrics.
// Thread-safe registration and access.
class CLOUD_ENGINE_API MetricsRegistry {
public:
	static MetricsRegistry& Instance();

	Counter& GetCounter(const std::string& name);
	Gauge& GetGauge(const std::string& name);
	Histogram& GetHistogram(const std::string& name, const std::vector<double>& buckets = {});

	// Export all metrics in Prometheus text format.
	std::string ExportPrometheus() const;

	// Export as JSON.
	std::string ExportJson() const;

	// Built-in metrics setup.
	void RegisterBuiltinMetrics();

	// Convenience accessors for built-in metrics.
	Counter& connections_total();
	Gauge& connections_active();
	Counter& messages_received_total();
	Counter& messages_sent_total();
	Counter& timers_fired_total();
	Counter& db_requests_total();
	Counter& db_requests_dropped_total();

private:
	MetricsRegistry() = default;
	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
	std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
	std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
};

}  // namespace monitoring
}  // namespace engine
