#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <vector>

namespace evpp {
class EventLoop;
}

namespace test {

// A synchronous, controllable EventLoop for unit tests.
// Queues RunInLoop/QueueInLoop callbacks and drains them via Pump().
// No real I/O, no threads — fully deterministic.
class FakeEventLoop {
public:
	using Functor = std::function<void()>;

	FakeEventLoop() = default;

	// ── API matching evpp::EventLoop ──────────────────────────────────

	void RunInLoop(Functor f) { queue_.push_back(std::move(f)); }
	void RunInLoop(const Functor& f) { queue_.push_back(f); }
	void QueueInLoop(Functor f) { queue_.push_back(std::move(f)); }
	void QueueInLoop(const Functor& f) { queue_.push_back(f); }

	void RunAfter(double delay_ms, Functor f) {
		timed_callbacks_.push_back({now_ + std::chrono::milliseconds(static_cast<int64_t>(delay_ms)), false, std::chrono::milliseconds(0), std::move(f)});
	}

	void RunEvery(std::chrono::nanoseconds interval, Functor f) {
		timed_callbacks_.push_back({now_, true, interval, std::move(f)});
	}

	void Stop() { stopped_ = true; }
	void Run() { while (!stopped_ && !queue_.empty()) { Pump(); } }

	bool IsInLoopThread() const { return true; }
	int pending_functor_count() const { return static_cast<int>(queue_.size()); }

	// ── Test controls ─────────────────────────────────────────────────

	// Drain all queued (RunInLoop/QueueInLoop) callbacks.
	void Pump() {
		auto pending = std::move(queue_);
		queue_.clear();
		for (auto& f : pending) { f(); }
	}

	// Advance virtual time by `d`, firing any due RunAfter/RunEvery callbacks.
	template <typename Rep, typename Period>
	void AdvanceTime(std::chrono::duration<Rep, Period> d) {
		now_ += std::chrono::duration_cast<std::chrono::nanoseconds>(d);
		for (auto& tc : timed_callbacks_) {
			if (tc.repeat) {
				// Fire repeating callbacks once per interval elapsed
				while (tc.next_fire <= now_) {
					tc.callback();
					tc.next_fire += tc.interval;
				}
			} else if (!tc.fired && tc.next_fire <= now_) {
				tc.fired = true;
				tc.callback();
			}
		}
		// Clean up fired one-shots
		timed_callbacks_.erase(
			std::remove_if(timed_callbacks_.begin(), timed_callbacks_.end(),
						   [](const TimedCallback& tc) { return tc.fired && !tc.repeat; }),
			timed_callbacks_.end());
	}

	std::chrono::nanoseconds Now() const { return now_; }

	// Number of pending RunAfter/RunEvery callbacks (includes repeats).
	size_t TimedCallbackCount() const { return timed_callbacks_.size(); }

private:
	struct TimedCallback {
		std::chrono::nanoseconds next_fire;
		bool fired = false;
		bool repeat = false;
		std::chrono::nanoseconds interval;
		Functor callback;
	};

	std::vector<Functor> queue_;
	std::vector<TimedCallback> timed_callbacks_;
	std::chrono::nanoseconds now_{0};
	bool stopped_ = false;
};

}  // namespace test
