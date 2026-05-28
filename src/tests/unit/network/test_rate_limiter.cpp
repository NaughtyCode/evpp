#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include <runtime/evpp/rate_limiter.h>

/* ============================================================================
 * RateLimiter — token-bucket unit tests (P3-3)
 * ============================================================================ */

TEST_CASE("RateLimiter unlimited mode returns full request", "[rate_limiter]") {
	evpp::RateLimiter rl(0);
	REQUIRE(rl.Consume(1000000) == 1000000);
	REQUIRE(rl.Consume(42) == 42);
}

TEST_CASE("RateLimiter consumes tokens correctly", "[rate_limiter]") {
	evpp::RateLimiter rl(1000);  // 1000 bytes/sec

	uint32_t allowed = rl.Consume(500);
	REQUIRE(allowed == 500);

	// Remaining tokens: 500. Request more than available.
	uint32_t allowed2 = rl.Consume(800);
	REQUIRE(allowed2 <= 500);
}

TEST_CASE("RateLimiter depletes tokens and returns zero", "[rate_limiter]") {
	evpp::RateLimiter rl(100);

	uint32_t first = rl.Consume(100);
	REQUIRE(first == 100);

	uint32_t second = rl.Consume(100);
	REQUIRE(second == 0);  // bucket empty, no time elapsed
}

TEST_CASE("RateLimiter refills tokens over time", "[rate_limiter]") {
	evpp::RateLimiter rl(2000);  // 2000 bytes/sec

	rl.Consume(2000);  // drain
	REQUIRE(rl.Consume(1) == 0);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	// After 50ms, ~100 tokens should have refilled
	uint32_t refilled = rl.Consume(2000);
	REQUIRE(refilled > 0);
	REQUIRE(refilled <= 2000);
}

TEST_CASE("RateLimiter SetRate resets bucket", "[rate_limiter]") {
	evpp::RateLimiter rl(500);
	rl.Consume(500);  // drain
	REQUIRE(rl.Consume(1) == 0);

	rl.SetRate(1000);  // reset to 1000
	REQUIRE(rl.Consume(1000) == 1000);
}

TEST_CASE("RateLimiter max_bytes_per_sec returns configured rate", "[rate_limiter]") {
	evpp::RateLimiter rl(65536);
	REQUIRE(rl.max_bytes_per_sec() == 65536);

	evpp::RateLimiter rl2;
	REQUIRE(rl2.max_bytes_per_sec() == 0);  // default unlimited
}

TEST_CASE("RateLimiter default constructor is unlimited", "[rate_limiter]") {
	evpp::RateLimiter rl;
	REQUIRE(rl.max_bytes_per_sec() == 0);
	REQUIRE(rl.Consume(999999) == 999999);
}

TEST_CASE("RateLimiter zero-byte consume does not drain tokens", "[rate_limiter]") {
	evpp::RateLimiter rl(1000);
	REQUIRE(rl.Consume(0) == 0);
	REQUIRE(rl.Consume(1000) == 1000);  // full tokens remain
}

TEST_CASE("RateLimiter large request clamped to available tokens", "[rate_limiter]") {
	evpp::RateLimiter rl(100);
	// Request more than the bucket capacity
	uint32_t allowed = rl.Consume(500);
	REQUIRE(allowed == 100);  // only 100 tokens available
	REQUIRE(rl.Consume(1) == 0);
}
