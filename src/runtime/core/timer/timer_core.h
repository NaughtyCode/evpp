// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// Fundamental time types and utilities for the GameTimer library.
// Adapted from Linux kernel timer subsystem concepts (ktime, timekeeping).

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <type_traits>

namespace engine {

// Duration type: nanosecond-resolution, mirrors Linux ktime_t concept

using Duration = std::chrono::nanoseconds;
using DurationDouble = std::chrono::duration<double>;

// Clock types mirroring Linux CLOCK_* identifiers
enum class ClockId : int {
	kMonotonic = 0,	 // CLOCK_MONOTONIC
	kRealtime = 1,	// CLOCK_REALTIME
	kBoottime = 2,	// CLOCK_BOOTTIME  (monotonic + suspend time)
	kTai = 3,  // CLOCK_TAI
	kRaw = 4,  // CLOCK_MONOTONIC_RAW

	// Aliases
	kDefault = kMonotonic,
};

inline const char* clock_id_name(ClockId id) {
	switch (id) {
	case ClockId::kMonotonic:
		return "monotonic";
	case ClockId::kRealtime:
		return "realtime";
	case ClockId::kBoottime:
		return "boottime";
	case ClockId::kTai:
		return "tai";
	case ClockId::kRaw:
		return "raw";
	default:
		return "unknown";
	}
}

// Time point types

// A generic time point using a specific clock type
// In practice, we use Duration as our "ktime_t" — a plain nanosecond count
// from an epoch. This decouples us from std::chrono clock types for
// flexibility (e.g., simulated time, testing).

using TimePoint = Duration;	 // nanoseconds from clock's epoch

// Time constants (matching Linux kernel NSEC_PER_SEC etc.)

inline constexpr int64_t kNsPerSec = 1'000'000'000;
inline constexpr int64_t kNsPerMs = 1'000'000;
inline constexpr int64_t kNsPerUs = 1'000;
inline constexpr int64_t kUsPerSec = 1'000'000;
inline constexpr int64_t kMsPerSec = 1'000;
inline constexpr int64_t kSecPerMin = 60;
inline constexpr int64_t kSecPerHour = 3600;
inline constexpr int64_t kSecPerDay = 86400;

inline constexpr TimePoint kTimeMax = TimePoint(INT64_MAX);
inline constexpr TimePoint kTimeMin = TimePoint(INT64_MIN);
inline constexpr TimePoint kTimeZero = TimePoint(0);

// Time construction helpers (mirror ktime_set, ns_to_ktime, etc.)

inline constexpr TimePoint make_time(int64_t secs, int64_t nsecs = 0) {
	// Check for overflow
	if (secs >= INT64_MAX / kNsPerSec) return kTimeMax;
	return TimePoint(secs * kNsPerSec + nsecs);
}

inline constexpr TimePoint ns_to_time(int64_t ns) {
	return TimePoint(ns);
}
inline constexpr TimePoint us_to_time(int64_t us) {
	return TimePoint(us * kNsPerUs);
}
inline constexpr TimePoint ms_to_time(int64_t ms) {
	return TimePoint(ms * kNsPerMs);
}
inline constexpr TimePoint sec_to_time(int64_t sec) {
	return TimePoint(sec * kNsPerSec);
}

inline constexpr int64_t time_to_ns(TimePoint t) {
	return t.count();
}
inline constexpr int64_t time_to_us(TimePoint t) {
	return t.count() / kNsPerUs;
}
inline constexpr int64_t time_to_ms(TimePoint t) {
	return t.count() / kNsPerMs;
}
inline constexpr int64_t time_to_sec(TimePoint t) {
	return t.count() / kNsPerSec;
}

// Time arithmetic (mirror ktime_add, ktime_sub, etc.)

inline constexpr TimePoint time_add(TimePoint a, TimePoint b) {
	return TimePoint(a.count() + b.count());
}

inline constexpr TimePoint time_sub(TimePoint a, TimePoint b) {
	return TimePoint(a.count() - b.count());
}

inline constexpr TimePoint time_add_ns(TimePoint t, int64_t ns) {
	return TimePoint(t.count() + ns);
}

inline constexpr TimePoint time_sub_ns(TimePoint t, int64_t ns) {
	return TimePoint(t.count() - ns);
}

// Safe addition that clamps on overflow (mirror ktime_add_safe)
inline TimePoint time_add_safe(TimePoint a, TimePoint b) {
	int64_t av = a.count();
	int64_t bv = b.count();
	// Portable overflow check for signed 64-bit addition
	if (bv > 0 && av > INT64_MAX - bv) {
		return kTimeMax;
	}
	if (bv < 0 && av < INT64_MIN - bv) {
		return kTimeMin;
	}
	return TimePoint(av + bv);
}

// Time comparison (mirror ktime_compare, ktime_before, ktime_after)

inline constexpr int time_compare(TimePoint a, TimePoint b) {
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

inline constexpr bool time_before(TimePoint a, TimePoint b) {
	return a < b;
}
inline constexpr bool time_after(TimePoint a, TimePoint b) {
	return a > b;
}
inline constexpr bool time_equal(TimePoint a, TimePoint b) {
	return a == b;
}

inline constexpr TimePoint time_min(TimePoint a, TimePoint b) {
	return a < b ? a : b;
}
inline constexpr TimePoint time_max(TimePoint a, TimePoint b) {
	return a > b ? a : b;
}

// Time delta helpers

inline constexpr int64_t time_delta_ns(TimePoint later, TimePoint earlier) {
	return (later - earlier).count();
}

inline constexpr int64_t time_delta_us(TimePoint later, TimePoint earlier) {
	return time_delta_ns(later, earlier) / kNsPerUs;
}

inline constexpr int64_t time_delta_ms(TimePoint later, TimePoint earlier) {
	return time_delta_ns(later, earlier) / kNsPerMs;
}

// Timer mode flags (mirror hrtimer_mode / TIMER_* flags)

enum class TimerMode : uint32_t {
	kAbsolute = 0x00,  // Expiry time is absolute
	kRelative = 0x01,  // Expiry time is relative to now

	kOneShot = 0x00,  // Timer fires once
	kRepeating = 0x02,	// Timer restarts after expiry

	kSoft = 0x04,  // Lower priority processing
	kHard = 0x08,  // Higher priority processing

	kPinned = 0x10,	 // Timer bound to a specific context
	kDeferrable = 0x20,	 // Timer can be deferred (timer wheel concept)

	// Compound flags
	kDefault = kAbsolute | kOneShot | kHard,
};

inline constexpr TimerMode operator|(TimerMode a, TimerMode b) {
	return static_cast<TimerMode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr TimerMode operator&(TimerMode a, TimerMode b) {
	return static_cast<TimerMode>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr bool mode_is_absolute(TimerMode m) {
	return (m & TimerMode::kRelative) == TimerMode::kAbsolute;
}
inline constexpr bool mode_is_relative(TimerMode m) {
	return (m & TimerMode::kRelative) == TimerMode::kRelative;
}
inline constexpr bool mode_is_repeating(TimerMode m) {
	return (m & TimerMode::kRepeating) == TimerMode::kRepeating;
}
inline constexpr bool mode_is_one_shot(TimerMode m) {
	return (m & TimerMode::kRepeating) == TimerMode::kOneShot;
}
inline constexpr bool mode_is_hard(TimerMode m) {
	return (m & TimerMode::kHard) == TimerMode::kHard;
}
inline constexpr bool mode_is_soft(TimerMode m) {
	return (m & TimerMode::kSoft) == TimerMode::kSoft;
}
inline constexpr bool mode_is_pinned(TimerMode m) {
	return (m & TimerMode::kPinned) == TimerMode::kPinned;
}
inline constexpr bool mode_is_deferrable(TimerMode m) {
	return (m & TimerMode::kDeferrable) == TimerMode::kDeferrable;
}

// Timer callback return values (mirror hrtimer_restart enum)

enum class TimerResult {
	kNoRestart,	 // Timer is not restarted (one-shot done)
	kRestart,  // Timer must be restarted (for repeating timers)
};

// Callback types

// High-resolution timer callback: returns whether to restart
template <typename TimerType>
using HrTimerCallback = TimerResult (*)(TimerType* timer);

// Simple void callback for timer wheel timers
template <typename TimerType>
using TimerCallback = void (*)(TimerType* timer);

// Generic std::function versions for flexibility
using GenericTimerFn = std::function<void()>;
using GenericTimerResultFn = std::function<TimerResult()>;

// Timer state

enum class TimerState : uint8_t {
	kInactive = 0,	// Timer is not armed
	kArmed = 1,	 // Timer is enqueued and waiting to fire
	kFiring = 2,  // Timer callback is currently executing
	kCancelled = 3,	 // Timer has been cancelled (transient)
};

inline const char* timer_state_name(TimerState s) {
	switch (s) {
	case TimerState::kInactive:
		return "inactive";
	case TimerState::kArmed:
		return "armed";
	case TimerState::kFiring:
		return "firing";
	case TimerState::kCancelled:
		return "cancelled";
	default:
		return "unknown";
	}
}

// Timer statistics

struct TimerStats {
	uint64_t total_armed = 0;
	uint64_t total_expired = 0;
	uint64_t total_cancelled = 0;
	uint64_t total_restarted = 0;
	int64_t min_latency_ns = INT64_MAX;
	int64_t max_latency_ns = 0;
	int64_t total_latency_ns = 0;
	uint64_t overrun_count = 0;

	void record_arm() {
		++total_armed;
	}
	void record_expire(int64_t latency_ns) {
		++total_expired;
		total_latency_ns += latency_ns;
		if (latency_ns < min_latency_ns) min_latency_ns = latency_ns;
		if (latency_ns > max_latency_ns) max_latency_ns = latency_ns;
	}
	void record_cancel() {
		++total_cancelled;
	}
	void record_restart() {
		++total_restarted;
	}
	void record_overrun() {
		++overrun_count;
	}

	double avg_latency_us() const {
		if (total_expired == 0) return 0.0;
		return static_cast<double>(total_latency_ns) / total_expired / 1000.0;
	}
};

// Utility: get current time from various clock sources

// Default clock (monotonic) — preferred for game timers
inline TimePoint clock_now_monotonic() {
	auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration_cast<Duration>(now);
}

inline TimePoint clock_now_realtime() {
	auto now = std::chrono::system_clock::now().time_since_epoch();
	return std::chrono::duration_cast<Duration>(now);
}

// High-performance now (uses rdtsc-like on x86, fallback to steady_clock)
inline TimePoint clock_now() {
	return clock_now_monotonic();
}

// Boot time (monotonic including suspend time) — platform dependent
inline TimePoint clock_now_boottime() {
	// On most platforms, steady_clock does NOT include suspend time.
	// For game use, this is typically the same as monotonic.
	return clock_now_monotonic();
}

inline TimePoint clock_now_raw() {
	// Raw monotonic — closest we can get in userspace
	return clock_now_monotonic();
}

// Get time for a specific clock ID
inline TimePoint clock_now_for(ClockId id) {
	switch (id) {
	case ClockId::kMonotonic:
		return clock_now_monotonic();
	case ClockId::kRealtime:
		return clock_now_realtime();
	case ClockId::kBoottime:
		return clock_now_boottime();
	case ClockId::kTai:
	case ClockId::kRaw:
		return clock_now_raw();
	default:
		return clock_now_monotonic();
	}
}

// High-resolution sleep helpers

inline void sleep_until(TimePoint target, ClockId clock = ClockId::kMonotonic) {
	auto now = clock_now_for(clock);
	if (target <= now) return;

	auto sleep_dur = target - now;
	std::this_thread::sleep_for(sleep_dur);
}

inline void sleep_for(Duration dur) {
	std::this_thread::sleep_for(dur);
}

}  // namespace engine
