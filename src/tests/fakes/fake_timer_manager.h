#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include "runtime/core/timer/timer_manager.h"

namespace test {

// Virtual-time TimerManager for deterministic unit tests.
// Timers are stored in a priority queue. Time only advances via AdvanceTime()
// or the Update(TimePoint) override — no real clock involved.
//
// Usage:
//   FakeTimerManager tm;
//   auto tid = tm.create_simple_timer([]() { ... });
//   tm.start_timer_relative(tid, std::chrono::milliseconds(100));
//   tm.AdvanceTime(std::chrono::milliseconds(150));  // fires the timer
class FakeTimerManager {
public:
	using TimerId = engine::TimerId;
	static constexpr TimerId kInvalidTimerId = engine::kInvalidTimerId;
	using TimePoint = engine::TimePoint;
	using Duration = engine::Duration;
	using UpdateResult = engine::TimerManager::UpdateResult;

	FakeTimerManager() = default;
	~FakeTimerManager() { shutdown(); }

	FakeTimerManager(const FakeTimerManager&) = delete;
	FakeTimerManager& operator=(const FakeTimerManager&) = delete;

	void initialize() { initialized_ = true; }
	void shutdown() {
		timers_.clear();
		next_id_ = 1;
		initialized_ = false;
	}
	bool is_initialized() const { return initialized_; }

	// ── Timer creation ─────────────────────────────────────────────────

	TimerId create_simple_timer(std::function<void()> callback,
								engine::ClockId clock_id = engine::ClockId::kMonotonic) {
		TimerId id = next_id_++;
		timers_[id] = TimerEntry{std::move(callback), false, Duration{0}};
		return id;
	}

	TimerId create_repeating_simple_timer(Duration interval,
										  std::function<void()> callback,
										  engine::ClockId clock_id = engine::ClockId::kMonotonic) {
		TimerId id = next_id_++;
		timers_[id] = TimerEntry{std::move(callback), true, interval};
		return id;
	}

	template <typename Rep, typename Period>
	TimerId create_timer_for(std::chrono::duration<Rep, Period> dur,
							 std::function<void()> callback) {
		TimerId id = create_simple_timer(std::move(callback));
		start_timer_relative(id, std::chrono::duration_cast<Duration>(dur));
		return id;
	}

	// ── Timer control ──────────────────────────────────────────────────

	void start_timer_relative(TimerId id, Duration rel_time,
							  engine::TimerMode mode = engine::TimerMode::kRelative) {
		auto it = timers_.find(id);
		if (it == timers_.end()) return;
		it->second.active = true;
		it->second.expiry = now_ + rel_time;
	}

	bool cancel_timer(TimerId id) {
		auto it = timers_.find(id);
		if (it == timers_.end()) return false;
		if (it->second.active) {
			it->second.active = false;
			return true;
		}
		return false;
	}

	bool is_timer_active(TimerId id) const {
		auto it = timers_.find(id);
		return it != timers_.end() && it->second.active;
	}

	// ── Time query ─────────────────────────────────────────────────────

	TimePoint now() const { return now_; }
	int64_t now_ns() const { return now_.time_since_epoch().count(); }

	// ── Update / Advance ───────────────────────────────────────────────

	UpdateResult update() { return update(now_); }

	UpdateResult update(TimePoint tp) {
		now_ = tp;
		UpdateResult result;
		result.current_time = now_;

		for (auto& pair : timers_) {
			auto& entry = pair.second;
			if (!entry.active) continue;
			if (entry.expiry <= now_) {
				entry.active = false;  // will be re-armed below if repeating
				if (entry.callback) entry.callback();
				result.total_fired++;

				if (entry.repeat) {
					entry.active = true;
					entry.expiry = now_ + entry.interval;
				}
			}
		}
		return result;
	}

	// Advance virtual time and fire all due timers (convenience).
	template <typename Rep, typename Period>
	UpdateResult AdvanceTime(std::chrono::duration<Rep, Period> d) {
		return update(now_ + std::chrono::duration_cast<Duration>(d));
	}

	// ── Introspection ──────────────────────────────────────────────────

	size_t timer_count() const { return timers_.size(); }

	size_t active_count() const {
		size_t n = 0;
		for (const auto& p : timers_) { if (p.second.active) ++n; }
		return n;
	}

	// ── Instance access (mirrors TimerManager::instance) ───────────────

	static FakeTimerManager& instance() {
		static FakeTimerManager inst;
		return inst;
	}

private:
	struct TimerEntry {
		std::function<void()> callback;
		bool repeat = false;
		Duration interval{0};
		bool active = false;
		TimePoint expiry{0};
	};

	std::unordered_map<TimerId, TimerEntry> timers_;
	TimerId next_id_ = 1;
	TimePoint now_{0};
	bool initialized_ = false;
};

}  // namespace test
