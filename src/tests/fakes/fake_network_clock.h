#pragma once

#include <chrono>
#include <cstdint>

namespace test {

// A controllable clock for deterministic time-based tests.
// Time starts at zero and only advances when explicitly told to.
class FakeNetworkClock {
public:
	using TimePoint = std::chrono::nanoseconds;
	using Duration = std::chrono::nanoseconds;

	TimePoint Now() const { return now_; }

	template <typename Rep, typename Period>
	void Advance(std::chrono::duration<Rep, Period> d) {
		now_ += std::chrono::duration_cast<Duration>(d);
	}

	void Set(TimePoint t) { now_ = t; }
	void Reset() { now_ = TimePoint{0}; }

private:
	TimePoint now_{0};
};

}  // namespace test
