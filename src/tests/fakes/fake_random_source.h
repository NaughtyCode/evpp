#pragma once

#include <cstdint>
#include <vector>

namespace test {

// A deterministic pseudo-random source using a simple LCG.
// Seed is controllable for reproducible test runs.
class FakeRandomSource {
public:
	explicit FakeRandomSource(uint64_t seed = 42) : state_(seed) {}

	// Reset the seed to a known value for reproducibility.
	void Seed(uint64_t seed) { state_ = seed; initial_seed_ = seed; }
	uint64_t InitialSeed() const { return initial_seed_; }

	// Generate a random uint64_t (LCG).
	uint64_t NextU64() {
		state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
		return state_;
	}

	// Uniform integer in [0, max].
	uint64_t NextU64(uint64_t max) {
		if (max == 0) return 0;
		return NextU64() % (max + 1);
	}

	// Uniform integer in [min, max].
	int64_t NextInt64(int64_t min, int64_t max) {
		return min + static_cast<int64_t>(NextU64(static_cast<uint64_t>(max - min)));
	}

	// Uniform float in [0.0, 1.0).
	double NextDouble() {
		return static_cast<double>(NextU64()) / static_cast<double>(UINT64_MAX);
	}

	// Generate `count` random bytes.
	std::vector<uint8_t> NextBytes(size_t count) {
		std::vector<uint8_t> out(count);
		for (size_t i = 0; i < count; ++i) {
			out[i] = static_cast<uint8_t>(NextU64(255));
		}
		return out;
	}

private:
	uint64_t state_;
	uint64_t initial_seed_ = 42;
};

}  // namespace test
