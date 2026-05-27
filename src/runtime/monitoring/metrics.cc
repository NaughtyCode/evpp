#include "runtime/monitoring/metrics.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace engine {
namespace monitoring {

Histogram::Histogram(const std::vector<double>& buckets)
	: buckets_(buckets)
	, bucket_counts_(buckets.size() + 1) {
}

void Histogram::Observe(double value) {
	count_.fetch_add(1, std::memory_order_relaxed);
	sum_.fetch_add(value, std::memory_order_relaxed);

	// Find the right bucket
	size_t i = 0;
	for (; i < buckets_.size(); ++i) {
		if (value <= buckets_[i]) break;
	}
	bucket_counts_[i].fetch_add(1, std::memory_order_relaxed);
}

double Histogram::Mean() const {
	int64_t c = Count();
	return c > 0 ? Sum() / static_cast<double>(c) : 0.0;
}

double Histogram::P50() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int64_t c = Count();
	if (c == 0) return 0.0;
	int64_t target = c / 2;
	int64_t cumulative = 0;
	for (size_t i = 0; i < bucket_counts_.size(); ++i) {
		cumulative += bucket_counts_[i].load(std::memory_order_relaxed);
		if (cumulative >= target) {
			return i < buckets_.size() ? buckets_[i] : buckets_.back() * 2.0;
		}
	}
	return 0.0;
}

double Histogram::P95() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int64_t c = Count();
	if (c == 0) return 0.0;
	int64_t target = c * 95 / 100;
	int64_t cumulative = 0;
	for (size_t i = 0; i < bucket_counts_.size(); ++i) {
		cumulative += bucket_counts_[i].load(std::memory_order_relaxed);
		if (cumulative >= target) {
			return i < buckets_.size() ? buckets_[i] : buckets_.back() * 2.0;
		}
	}
	return 0.0;
}

double Histogram::P99() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int64_t c = Count();
	if (c == 0) return 0.0;
	int64_t target = c * 99 / 100;
	int64_t cumulative = 0;
	for (size_t i = 0; i < bucket_counts_.size(); ++i) {
		cumulative += bucket_counts_[i].load(std::memory_order_relaxed);
		if (cumulative >= target) {
			return i < buckets_.size() ? buckets_[i] : buckets_.back() * 2.0;
		}
	}
	return 0.0;
}

MetricsRegistry& MetricsRegistry::Instance() {
	static MetricsRegistry instance;
	return instance;
}

Counter& MetricsRegistry::GetCounter(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = counters_.find(name);
	if (it != counters_.end()) return *it->second;
	auto counter = std::make_unique<Counter>();
	auto* raw = counter.get();
	counters_[name] = std::move(counter);
	return *raw;
}

Gauge& MetricsRegistry::GetGauge(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = gauges_.find(name);
	if (it != gauges_.end()) return *it->second;
	auto gauge = std::make_unique<Gauge>();
	auto* raw = gauge.get();
	gauges_[name] = std::move(gauge);
	return *raw;
}

Histogram& MetricsRegistry::GetHistogram(const std::string& name,
										  const std::vector<double>& buckets) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = histograms_.find(name);
	if (it != histograms_.end()) return *it->second;
	auto hist = std::make_unique<Histogram>(buckets);
	auto* raw = hist.get();
	histograms_[name] = std::move(hist);
	return *raw;
}

std::string MetricsRegistry::ExportPrometheus() const {
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream oss;

	for (const auto& [name, counter] : counters_) {
		oss << "# TYPE " << name << " counter\n";
		oss << name << " " << counter->Value() << "\n";
	}

	for (const auto& [name, gauge] : gauges_) {
		oss << "# TYPE " << name << " gauge\n";
		oss << name << " " << gauge->Value() << "\n";
	}

	for (const auto& [name, hist] : histograms_) {
		oss << "# TYPE " << name << " histogram\n";
		oss << name << "_count " << hist->Count() << "\n";
		oss << name << "_sum " << hist->Sum() << "\n";
	}

	return oss.str();
}

std::string MetricsRegistry::ExportJson() const {
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream oss;
	oss << "{";

	bool first = true;
	for (const auto& [name, counter] : counters_) {
		if (!first) oss << ",";
		oss << "\"" << name << "\":" << counter->Value();
		first = false;
	}
	for (const auto& [name, gauge] : gauges_) {
		if (!first) oss << ",";
		oss << "\"" << name << "\":" << gauge->Value();
		first = false;
	}

	oss << "}";
	return oss.str();
}

void MetricsRegistry::RegisterBuiltinMetrics() {
	GetCounter("evpp_connections_total");
	GetGauge("evpp_connections_active");
	GetCounter("evpp_messages_received_total");
	GetCounter("evpp_messages_sent_total");
	GetHistogram("evpp_message_size_bytes", {64, 256, 1024, 4096, 16384, 65536});
	GetGauge("evpp_timers_active");
	GetCounter("evpp_timers_fired_total");
	GetCounter("db_requests_total");
	GetCounter("db_requests_dropped_total");
	GetHistogram("db_request_latency_ms", {1, 5, 10, 50, 100, 500, 1000});
	GetGauge("lua_vm_memory_kb");
	GetCounter("lua_gc_count_total");
	GetGauge("engine_uptime_seconds");
	GetGauge("physics_bodies_active");
	GetHistogram("physics_step_latency_us", {100, 500, 1000, 5000, 10000, 50000});
}

Counter& MetricsRegistry::connections_total() { return GetCounter("evpp_connections_total"); }
Gauge& MetricsRegistry::connections_active() { return GetGauge("evpp_connections_active"); }
Counter& MetricsRegistry::messages_received_total() { return GetCounter("evpp_messages_received_total"); }
Counter& MetricsRegistry::messages_sent_total() { return GetCounter("evpp_messages_sent_total"); }
Counter& MetricsRegistry::timers_fired_total() { return GetCounter("evpp_timers_fired_total"); }
Counter& MetricsRegistry::db_requests_total() { return GetCounter("db_requests_total"); }
Counter& MetricsRegistry::db_requests_dropped_total() { return GetCounter("db_requests_dropped_total"); }

}  // namespace monitoring
}  // namespace engine
