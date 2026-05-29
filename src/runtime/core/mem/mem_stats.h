#pragma once

#ifdef ENGINE_MEM_STATS_ENABLED

#include "runtime/core/engine_api.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
namespace mem {

// ── Operation types ──────────────────────────────────────────────────────

enum class MemOp : uint8_t {
	kNew,
	kNewArr,
	kNewNothrow,
	kNewArrNothrow,
	kMalloc,
	kCalloc,
	kRealloc,
	kDelete,
	kDeleteArr,
	kFree,
};

// ── Per-operation counters ───────────────────────────────────────────────

struct MemOpCounters {
	std::atomic<uint64_t> alloc_count{0};
	std::atomic<uint64_t> free_count{0};
	std::atomic<uint64_t> alloc_bytes{0};
	std::atomic<uint64_t> free_bytes{0};
};

// ── Size bucket index ────────────────────────────────────────────────────

enum class SizeBucket : uint8_t {
	kLt64,
	k64_256,
	k256_1k,
	k1k_4k,
	k4k_64k,
	kGt64k,
	kCount,
};

// ── Allocation record (stored in pointer→info map) ───────────────────────

struct AllocRecord {
	size_t size;
	MemOp op;
	const char* file;
	int line;
	std::chrono::steady_clock::time_point time;
};

// ── Call-site entry (ring buffer for recent allocs) ──────────────────────

struct CallSiteEntry {
	const char* file;
	int line;
	size_t size;
	MemOp op;
	std::chrono::steady_clock::time_point time;
};

// ── Coherent snapshot (all values read under mutex) ──────────────────────

struct CLOUD_ENGINE_API StatsSnapshot {
	// Totals
	uint64_t total_alloc_count = 0;
	uint64_t total_free_count = 0;
	uint64_t total_alloc_bytes = 0;
	uint64_t total_free_bytes = 0;
	uint64_t current_bytes = 0;
	uint64_t peak_bytes = 0;
	uint64_t peak_alloc_count = 0;

	// Per-operation
	uint64_t op_alloc_count[10] = {};
	uint64_t op_free_count[10] = {};
	uint64_t op_alloc_bytes[10] = {};
	uint64_t op_free_bytes[10] = {};

	// Size buckets
	uint64_t bucket_alloc_count[6] = {};
	uint64_t bucket_alloc_bytes[6] = {};

	// Timestamps
	int64_t start_time_us = 0;
	int64_t last_alloc_time_us = 0;
	int64_t last_free_time_us = 0;

	// Recent call sites
	std::vector<CallSiteEntry> recent_allocs;

	// Remaining entries in pointer map (leaked / not-yet-freed)
	uint64_t active_alloc_count = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// MemStats — thread-safe singleton accumulator
// ═══════════════════════════════════════════════════════════════════════════

class CLOUD_ENGINE_API MemStats {
	public:
	static MemStats& Instance();

	// Record an allocation.  Called from MEM_* macros when stats are on.
	void RecordAlloc(void* ptr, size_t bytes, MemOp op, const char* file,
	                 int line);

	// Record a deallocation.
	void RecordFree(void* ptr, MemOp op);

	// Record a realloc (old pointer freed, new pointer allocated).
	void RecordRealloc(void* old_ptr, void* new_ptr, size_t new_size,
	                   const char* file, int line);

	// Take a consistent snapshot of all counters.
	StatsSnapshot Snapshot() const;

	// Reset all counters to zero (keeps tracking on).
	void Reset();

	// Serialize snapshot as JSON and write to file.
	bool DumpToFile(const std::string& path) const;

	static const char* OpName(MemOp op);
	static const char* BucketName(SizeBucket b);

	private:
	MemStats() = default;
	~MemStats() = default;
	MemStats(const MemStats&) = delete;
	MemStats& operator=(const MemStats&) = delete;

	static SizeBucket BucketForSize(size_t bytes);
	void ToJson(const StatsSnapshot& snap, std::string& out) const;

	// ── Atomic counters (fast path — relaxed ordering) ─────────────────

	std::atomic<uint64_t> total_alloc_count_{0};
	std::atomic<uint64_t> total_free_count_{0};
	std::atomic<uint64_t> total_alloc_bytes_{0};
	std::atomic<uint64_t> total_free_bytes_{0};
	std::atomic<uint64_t> current_bytes_{0};
	std::atomic<uint64_t> peak_bytes_{0};
	std::atomic<uint64_t> peak_alloc_count_{0};

	MemOpCounters op_counters_[10];

	std::atomic<uint64_t> bucket_alloc_count_[6]{};
	std::atomic<uint64_t> bucket_alloc_bytes_[6]{};

	std::atomic<int64_t> start_time_us_{0};
	std::atomic<int64_t> last_alloc_time_us_{0};
	std::atomic<int64_t> last_free_time_us_{0};

	// ── Pointer → AllocRecord map (mutex-protected) ────────────────────

	mutable std::mutex map_mutex_;
	std::unordered_map<void*, AllocRecord> alloc_map_;

	// ── Ring buffer for recent call-site entries ───────────────────────

	static constexpr size_t kRingSize = 256;
	mutable std::mutex ring_mutex_;
	CallSiteEntry ring_buffer_[kRingSize]{};
	size_t ring_write_pos_ = 0;
	size_t ring_count_ = 0;
};

}  // namespace mem
}  // namespace engine

#endif  // ENGINE_MEM_STATS_ENABLED
