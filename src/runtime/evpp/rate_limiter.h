#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace evpp {

/* Token-bucket rate limiter for per-connection bandwidth control.
 *
 * Tokens refill at a constant rate (max_bytes_per_sec). Each call to
 * Consume() deducts tokens. When the bucket is empty, Consume() returns
 * the number of bytes currently allowed (possibly 0). Excess bytes
 * must be queued by the caller for the next tick.
 *
 * Thread-safe: Consume() and SetRate() may be called from any thread. */
class RateLimiter {
	public:
	RateLimiter() : max_bytes_per_sec_(0), tokens_(0) {
		last_refill_time_ = std::chrono::steady_clock::now();
	}

	explicit RateLimiter(uint32_t max_bytes_per_sec)
		: max_bytes_per_sec_(max_bytes_per_sec), tokens_(max_bytes_per_sec) {
		last_refill_time_ = std::chrono::steady_clock::now();
	}

	/* Set the rate limit in bytes per second. 0 = unlimited. */
	void SetRate(uint32_t max_bytes_per_sec) {
		max_bytes_per_sec_ = max_bytes_per_sec;
		tokens_ = max_bytes_per_sec;
		last_refill_time_ = std::chrono::steady_clock::now();
	}

	/* Return the number of bytes allowed to send right now.
	 * The caller should send up to this many bytes and queue the rest. */
	uint32_t Consume(uint32_t requested_bytes) {
		if (max_bytes_per_sec_ == 0) return requested_bytes;  // unlimited
		Refill();
		uint32_t allowed = (std::min)(requested_bytes, tokens_);
		tokens_ -= allowed;
		return allowed;
	}

	uint32_t max_bytes_per_sec() const { return max_bytes_per_sec_; }

	private:
	void Refill() {
		auto now = std::chrono::steady_clock::now();
		auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
							  now - last_refill_time_).count();
		if (elapsed_ns <= 0) return;

		/* tokens += (elapsed_seconds * max_bytes_per_sec) */
		uint64_t new_tokens = static_cast<uint64_t>(max_bytes_per_sec_) *
							  static_cast<uint64_t>(elapsed_ns) / 1'000'000'000ULL;
		if (new_tokens > 0) {
			tokens_ = (std::min)(tokens_ + static_cast<uint32_t>(new_tokens), max_bytes_per_sec_);
			last_refill_time_ = now;
		}
	}

	uint32_t max_bytes_per_sec_;
	uint32_t tokens_;
	std::chrono::steady_clock::time_point last_refill_time_;
};

}  // namespace evpp
