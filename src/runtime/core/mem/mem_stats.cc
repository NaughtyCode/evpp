#ifdef ENGINE_MEM_STATS_ENABLED

#include "runtime/core/mem/mem_stats.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

namespace engine {
namespace mem {

namespace {

int64_t NowUs() {
	auto now = std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<int64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void AppendJsonString(const char* s, std::string& out) {
	out += '"';
	for (const char* p = s; *p; ++p) {
		switch (*p) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += *p;
		}
	}
	out += '"';
}

void AppendJsonU64(const char* key, uint64_t val, std::string& out,
                    bool comma = true) {
	out += '"';
	out += key;
	out += "\": ";
	out += std::to_string(val);
	if (comma) out += ',';
	out += '\n';
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════

MemStats& MemStats::Instance() {
	static MemStats s;
	return s;
}

// ═══════════════════════════════════════════════════════════════════════════

SizeBucket MemStats::BucketForSize(size_t bytes) {
	if (bytes < 64)       return SizeBucket::kLt64;
	if (bytes < 256)      return SizeBucket::k64_256;
	if (bytes < 1024)     return SizeBucket::k256_1k;
	if (bytes < 4096)     return SizeBucket::k1k_4k;
	if (bytes < 65536)    return SizeBucket::k4k_64k;
	return SizeBucket::kGt64k;
}

const char* MemStats::OpName(MemOp op) {
	switch (op) {
		case MemOp::kNew:            return "new";
		case MemOp::kNewArr:         return "new_arr";
		case MemOp::kNewNothrow:     return "new_nothrow";
		case MemOp::kNewArrNothrow:  return "new_arr_nothrow";
		case MemOp::kMalloc:         return "malloc";
		case MemOp::kCalloc:         return "calloc";
		case MemOp::kRealloc:        return "realloc";
		case MemOp::kDelete:         return "delete";
		case MemOp::kDeleteArr:      return "delete_arr";
		case MemOp::kFree:           return "free";
	}
	return "unknown";
}

const char* MemStats::BucketName(SizeBucket b) {
	switch (b) {
		case SizeBucket::kLt64:   return "lt_64";
		case SizeBucket::k64_256: return "64_256";
		case SizeBucket::k256_1k: return "256_1k";
		case SizeBucket::k1k_4k:  return "1k_4k";
		case SizeBucket::k4k_64k: return "4k_64k";
		case SizeBucket::kGt64k:  return "gt_64k";
		default:                  return "?";
	}
}

// ═══════════════════════════════════════════════════════════════════════════

void MemStats::RecordAlloc(void* ptr, size_t bytes, MemOp op, const char* file,
                            int line) {
	if (!ptr) return;

	int64_t now = NowUs();
	if (start_time_us_.load(std::memory_order_relaxed) == 0) {
		start_time_us_.store(now, std::memory_order_relaxed);
	}

	// Totals
	total_alloc_count_.fetch_add(1, std::memory_order_relaxed);
	total_alloc_bytes_.fetch_add(bytes, std::memory_order_relaxed);
	uint64_t cur = current_bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;

	// Peak bytes
	uint64_t prev_peak = peak_bytes_.load(std::memory_order_relaxed);
	while (cur > prev_peak &&
	       !peak_bytes_.compare_exchange_weak(prev_peak, cur, std::memory_order_relaxed)) {
	}

	// Peak alloc count (approximate: alloc_count - free_count)
	uint64_t active = total_alloc_count_.load(std::memory_order_relaxed)
	                - total_free_count_.load(std::memory_order_relaxed);
	uint64_t prev_peak_count = peak_alloc_count_.load(std::memory_order_relaxed);
	while (active > prev_peak_count &&
	       !peak_alloc_count_.compare_exchange_weak(prev_peak_count, active,
	                                                 std::memory_order_relaxed)) {
	}

	last_alloc_time_us_.store(now, std::memory_order_relaxed);

	// Per-operation
	int op_idx = static_cast<int>(op);
	op_counters_[op_idx].alloc_count.fetch_add(1, std::memory_order_relaxed);
	op_counters_[op_idx].alloc_bytes.fetch_add(bytes, std::memory_order_relaxed);

	// Size bucket
	auto bucket = BucketForSize(bytes);
	int b_idx = static_cast<int>(bucket);
	bucket_alloc_count_[b_idx].fetch_add(1, std::memory_order_relaxed);
	bucket_alloc_bytes_[b_idx].fetch_add(bytes, std::memory_order_relaxed);

	// Pointer map
	{
		std::lock_guard<std::mutex> lk(map_mutex_);
		alloc_map_[ptr] = AllocRecord{bytes, op, file, line,
		                               std::chrono::steady_clock::time_point(
		                                   std::chrono::microseconds(now))};
	}

	// Ring buffer
	{
		std::lock_guard<std::mutex> lk(ring_mutex_);
		ring_buffer_[ring_write_pos_] = CallSiteEntry{file, line, bytes, op,
		                                               std::chrono::steady_clock::time_point(
		                                                   std::chrono::microseconds(now))};
		ring_write_pos_ = (ring_write_pos_ + 1) % kRingSize;
		if (ring_count_ < kRingSize) ++ring_count_;
	}
}

void MemStats::RecordFree(void* ptr, MemOp op) {
	if (!ptr) return;

	int64_t now = NowUs();
	total_free_count_.fetch_add(1, std::memory_order_relaxed);
	last_free_time_us_.store(now, std::memory_order_relaxed);

	int op_idx = static_cast<int>(op);
	op_counters_[op_idx].free_count.fetch_add(1, std::memory_order_relaxed);

	// Look up original alloc size from pointer map
	{
		std::lock_guard<std::mutex> lk(map_mutex_);
		auto it = alloc_map_.find(ptr);
		if (it != alloc_map_.end()) {
			size_t bytes = it->second.size;
			total_free_bytes_.fetch_add(bytes, std::memory_order_relaxed);
			op_counters_[op_idx].free_bytes.fetch_add(bytes, std::memory_order_relaxed);
			current_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
			alloc_map_.erase(it);
		}
	}
}

void MemStats::RecordRealloc(void* old_ptr, void* new_ptr, size_t new_size,
                              const char* file, int line) {
	if (old_ptr) {
		RecordFree(old_ptr, MemOp::kRealloc);
	}
	if (new_ptr) {
		RecordAlloc(new_ptr, new_size, MemOp::kRealloc, file, line);
	}
}

// ═══════════════════════════════════════════════════════════════════════════

StatsSnapshot MemStats::Snapshot() const {
	StatsSnapshot snap;

	snap.total_alloc_count = total_alloc_count_.load(std::memory_order_relaxed);
	snap.total_free_count  = total_free_count_.load(std::memory_order_relaxed);
	snap.total_alloc_bytes = total_alloc_bytes_.load(std::memory_order_relaxed);
	snap.total_free_bytes  = total_free_bytes_.load(std::memory_order_relaxed);
	snap.current_bytes     = current_bytes_.load(std::memory_order_relaxed);
	snap.peak_bytes        = peak_bytes_.load(std::memory_order_relaxed);
	snap.peak_alloc_count  = peak_alloc_count_.load(std::memory_order_relaxed);

	for (int i = 0; i < 10; ++i) {
		snap.op_alloc_count[i] = op_counters_[i].alloc_count.load(std::memory_order_relaxed);
		snap.op_free_count[i]  = op_counters_[i].free_count.load(std::memory_order_relaxed);
		snap.op_alloc_bytes[i] = op_counters_[i].alloc_bytes.load(std::memory_order_relaxed);
		snap.op_free_bytes[i]  = op_counters_[i].free_bytes.load(std::memory_order_relaxed);
	}

	for (int i = 0; i < 6; ++i) {
		snap.bucket_alloc_count[i] = bucket_alloc_count_[i].load(std::memory_order_relaxed);
		snap.bucket_alloc_bytes[i] = bucket_alloc_bytes_[i].load(std::memory_order_relaxed);
	}

	snap.start_time_us      = start_time_us_.load(std::memory_order_relaxed);
	snap.last_alloc_time_us = last_alloc_time_us_.load(std::memory_order_relaxed);
	snap.last_free_time_us  = last_free_time_us_.load(std::memory_order_relaxed);

	// Ring buffer — copy under lock
	{
		std::lock_guard<std::mutex> lk(ring_mutex_);
		snap.recent_allocs.reserve(ring_count_);
		size_t start = (ring_count_ < kRingSize) ? 0 : ring_write_pos_;
		size_t count = ring_count_;
		for (size_t i = 0; i < count; ++i) {
			snap.recent_allocs.push_back(
			    ring_buffer_[(start + i) % kRingSize]);
		}
	}

	// Active allocs count
	{
		std::lock_guard<std::mutex> lk(map_mutex_);
		snap.active_alloc_count = alloc_map_.size();
	}

	return snap;
}

void MemStats::Reset() {
	total_alloc_count_.store(0, std::memory_order_relaxed);
	total_free_count_.store(0, std::memory_order_relaxed);
	total_alloc_bytes_.store(0, std::memory_order_relaxed);
	total_free_bytes_.store(0, std::memory_order_relaxed);
	current_bytes_.store(0, std::memory_order_relaxed);
	peak_bytes_.store(0, std::memory_order_relaxed);
	peak_alloc_count_.store(0, std::memory_order_relaxed);

	for (int i = 0; i < 10; ++i) {
		op_counters_[i].alloc_count.store(0, std::memory_order_relaxed);
		op_counters_[i].free_count.store(0, std::memory_order_relaxed);
		op_counters_[i].alloc_bytes.store(0, std::memory_order_relaxed);
		op_counters_[i].free_bytes.store(0, std::memory_order_relaxed);
	}

	for (int i = 0; i < 6; ++i) {
		bucket_alloc_count_[i].store(0, std::memory_order_relaxed);
		bucket_alloc_bytes_[i].store(0, std::memory_order_relaxed);
	}

	start_time_us_.store(0, std::memory_order_relaxed);
	last_alloc_time_us_.store(0, std::memory_order_relaxed);
	last_free_time_us_.store(0, std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lk(map_mutex_);
		alloc_map_.clear();
	}
	{
		std::lock_guard<std::mutex> lk(ring_mutex_);
		ring_write_pos_ = 0;
		ring_count_ = 0;
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// JSON serialization (hand-rolled — no third-party JSON lib dependency)
// ═══════════════════════════════════════════════════════════════════════════

void MemStats::ToJson(const StatsSnapshot& snap, std::string& out) const {
	out.reserve(4096);

	// Collect non-empty ops so we can write them with correct comma placement.
	int ops_to_write[9];
	int op_count = 0;
	for (int i = 0; i < 9; ++i) {
		if (snap.op_alloc_count[i] != 0 || snap.op_free_count[i] != 0) {
			ops_to_write[op_count++] = i;
		}
	}

	out += "{\n";

	// Timestamp
	{
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		char buf[32];
		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
		out += "\"timestamp\": ";
		AppendJsonString(buf, out);
		out += ",\n";
	}

	// ── Totals ───────────────────────────────────────────────────────
	out += "\"totals\": {\n";
	AppendJsonU64("alloc_count", snap.total_alloc_count, out);
	AppendJsonU64("free_count", snap.total_free_count, out);
	AppendJsonU64("alloc_bytes", snap.total_alloc_bytes, out);
	AppendJsonU64("free_bytes", snap.total_free_bytes, out);
	AppendJsonU64("bytes_in_use", snap.current_bytes, out);
	AppendJsonU64("peak_bytes", snap.peak_bytes, out);
	AppendJsonU64("peak_alloc_count", snap.peak_alloc_count, out, false);
	out += "},\n";

	// ── Per-operation ────────────────────────────────────────────────
	out += "\"by_operation\": {\n";
	for (int k = 0; k < op_count; ++k) {
		int i = ops_to_write[k];
		out += "  ";
		AppendJsonString(OpName(static_cast<MemOp>(i)), out);
		out += ": {\n";
		out += "    ";
		AppendJsonU64("alloc_count", snap.op_alloc_count[i], out);
		out += "    ";
		AppendJsonU64("free_count", snap.op_free_count[i], out);
		out += "    ";
		AppendJsonU64("alloc_bytes", snap.op_alloc_bytes[i], out);
		out += "    ";
		AppendJsonU64("free_bytes", snap.op_free_bytes[i], out, false);
		out += "  }";
		out += (k < op_count - 1) ? ",\n" : "\n";
	}
	out += "},\n";

	// ── Size buckets ─────────────────────────────────────────────────
	out += "\"size_buckets\": {\n";
	for (int i = 0; i < 6; ++i) {
		out += "  ";
		AppendJsonString(BucketName(static_cast<SizeBucket>(i)), out);
		out += ": {\n";
		out += "    ";
		AppendJsonU64("count", snap.bucket_alloc_count[i], out);
		out += "    ";
		AppendJsonU64("bytes", snap.bucket_alloc_bytes[i], out, false);
		out += "  }";
		out += (i < 5) ? ",\n" : "\n";
	}
	out += "},\n";

	// ── Active allocations ───────────────────────────────────────────
	AppendJsonU64("active_alloc_count", snap.active_alloc_count, out, false);
	out += "}\n";
}

bool MemStats::DumpToFile(const std::string& path) const {
	StatsSnapshot snap = Snapshot();

	std::string json;
	ToJson(snap, json);

	std::ofstream ofs(path, std::ios::out | std::ios::trunc);
	if (!ofs.is_open()) return false;

	ofs << json;
	ofs.close();
	return ofs.good();
}

}  // namespace mem
}  // namespace engine

#endif  // ENGINE_MEM_STATS_ENABLED
