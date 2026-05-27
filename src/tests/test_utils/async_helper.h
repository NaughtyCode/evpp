#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace test {

// Polls a predicate until it returns true or timeout_ms elapses.
// Returns true if the predicate succeeded, false on timeout.
//
// Usage:
//   bool done = WaitFor([]() { return some_flag; }, 5000);
//   REQUIRE(done);
inline bool WaitFor(std::function<bool()> predicate, int timeout_ms = 5000) {
	auto start = std::chrono::steady_clock::now();
	auto deadline = start + std::chrono::milliseconds(timeout_ms);

	while (std::chrono::steady_clock::now() < deadline) {
		if (predicate()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return predicate();  // one last check
}

// Repeatedly invokes `action` until `predicate` returns true or timeout_ms
// elapses. Useful for producer/consumer patterns.
inline bool WaitForAction(std::function<bool()> predicate,
						  std::function<void()> action,
						  int timeout_ms = 5000) {
	auto start = std::chrono::steady_clock::now();
	auto deadline = start + std::chrono::milliseconds(timeout_ms);

	while (std::chrono::steady_clock::now() < deadline) {
		action();
		if (predicate()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return predicate();
}

}  // namespace test
