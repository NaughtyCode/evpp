// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// Clock source abstraction layer. Mirrors the Linux kernel clocksource
// subsystem (include/linux/clocksource.h, kernel/time/clocksource.c).
//
// Provides:
//  - ClockSource: abstract time source with rating/priority
//  - ClockEvent: one-shot / periodic event device abstraction
//  - ClockManager: manages the active clock source and event device

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/core/timer/timer_core.h"

namespace engine {

//=============================================================================
// ClockSource — abstraction for a free-running time counter
//=============================================================================

class ClockSource {
	public:
	using ReadFunc = std::function<TimePoint()>;

	ClockSource() = default;

	// Construct with a name, rating, and read function
	ClockSource(std::string name, int rating, ReadFunc reader)
		: name_(std::move(name)),
		  rating_(rating),
		  read_fn_(std::move(reader)),
		  freq_khz_(0),
		  shift_(0),
		  mult_(1),
		  mask_(0xFFFFFFFFFFFFFFFFULL),
		  max_idle_ns_(0),
		  flags_(0) {
	}

	virtual ~ClockSource() = default;

	// Read the current counter value as a TimePoint
	virtual TimePoint read() const {
		return read_fn_ ? read_fn_() : clock_now_monotonic();
	}

	// Clock source metadata
	const std::string& name() const {
		return name_;
	}
	int rating() const {
		return rating_;
	}
	uint32_t freq_khz() const {
		return freq_khz_;
	}
	uint32_t mult() const {
		return mult_;
	}
	uint32_t shift() const {
		return shift_;
	}
	uint64_t mask() const {
		return mask_;
	}
	uint64_t max_idle_ns() const {
		return max_idle_ns_;
	}
	unsigned long flags() const {
		return flags_;
	}

	// Flags
	bool is_continuous() const {
		return (flags_ & kFlagContinuous) != 0;
	}
	bool is_valid_for_hres() const {
		return (flags_ & kFlagValidForHres) != 0;
	}
	bool is_unstable() const {
		return (flags_ & kFlagUnstable) != 0;
	}
	bool suspend_nonstop() const {
		return (flags_ & kFlagSuspendNonstop) != 0;
	}

	void set_rating(int r) {
		rating_ = r;
	}
	void set_flags(unsigned long f) {
		flags_ = f;
	}
	void add_flag(unsigned long f) {
		flags_ |= f;
	}
	void clear_flag(unsigned long f) {
		flags_ &= ~f;
	}

	// Conversion: cycles -> nanoseconds
	int64_t cyc_to_ns(uint64_t cycles) const {
		return static_cast<int64_t>((cycles * mult_) >> shift_);
	}

	// Compute max safe delta (mirrors clocks_calc_max_nsecs)
	void calc_mult_shift(uint32_t from_hz) {
		// mult = (NSEC_PER_SEC << shift) / freq
		// We want shift large enough for precision, small enough to avoid overflow
		shift_ = 24;  // reasonable default
		uint64_t tmp = (static_cast<uint64_t>(kNsPerSec) << shift_);
		mult_ = static_cast<uint32_t>(tmp / from_hz);
	}

	// Rating constants for clock source quality
	static constexpr int kRatingUnusable = 0;
	static constexpr int kRatingPoor = 100;
	static constexpr int kRatingAdequate = 200;
	static constexpr int kRatingGood = 300;
	static constexpr int kRatingExcellent = 400;

	// Flag constants
	static constexpr unsigned long kFlagContinuous = 0x01;
	static constexpr unsigned long kFlagValidForHres = 0x20;
	static constexpr unsigned long kFlagUnstable = 0x40;
	static constexpr unsigned long kFlagSuspendNonstop = 0x80;

	protected:
	std::string name_;
	int rating_ = 0;
	ReadFunc read_fn_;
	uint32_t freq_khz_ = 0;
	uint32_t shift_ = 0;
	uint32_t mult_ = 1;
	uint64_t mask_ = ~0ULL;
	uint64_t max_idle_ns_ = 0;
	unsigned long flags_ = 0;
};

//=============================================================================
// Concrete clock sources
//=============================================================================

// Monotonic clock source (default, recommended for games)
class MonotonicClockSource : public ClockSource {
	public:
	MonotonicClockSource()
		: ClockSource(
			  "monotonic", ClockSource::kRatingExcellent, []() { return clock_now_monotonic(); }) {
		add_flag(kFlagContinuous | kFlagValidForHres);
	}
};

// Realtime clock source
class RealtimeClockSource : public ClockSource {
	public:
	RealtimeClockSource()
		: ClockSource("realtime", ClockSource::kRatingGood, []() { return clock_now_realtime(); }) {
		add_flag(kFlagContinuous);
	}
};

//=============================================================================
// ClockEventDevice — abstraction for a programmable timer event device
// (mirrors clock_event_device in Linux)
//=============================================================================

class ClockEventDevice {
	public:
	enum class State {
		kDetached,
		kShutdown,
		kPeriodic,
		kOneShot,
		kOneShotStopped,
	};

	using EventHandler = std::function<void()>;

	ClockEventDevice() = default;
	virtual ~ClockEventDevice() = default;

	// Set the next event (oneshot mode)
	virtual int set_next_event(TimePoint expires) {
		next_event_ = expires;
		return 0;
	}

	// Switch to periodic mode
	virtual int set_state_periodic() {
		state_ = State::kPeriodic;
		return 0;
	}

	// Switch to oneshot mode
	virtual int set_state_oneshot() {
		state_ = State::kOneShot;
		return 0;
	}

	// Shutdown the device
	virtual int set_state_shutdown() {
		state_ = State::kShutdown;
		return 0;
	}

	// Accessors
	State state() const {
		return state_;
	}
	TimePoint next_event() const {
		return next_event_;
	}
	const std::string& name() const {
		return name_;
	}
	int rating() const {
		return rating_;
	}
	unsigned int features() const {
		return features_;
	}
	int64_t min_delta_ns() const {
		return min_delta_ns_;
	}
	int64_t max_delta_ns() const {
		return max_delta_ns_;
	}
	uint32_t mult() const {
		return mult_;
	}
	uint32_t shift() const {
		return shift_;
	}

	void set_event_handler(EventHandler h) {
		event_handler_ = std::move(h);
	}
	void set_name(std::string n) {
		name_ = std::move(n);
	}
	void set_rating(int r) {
		rating_ = r;
	}
	void set_features(unsigned int f) {
		features_ = f;
	}
	void set_min_delta_ns(int64_t ns) {
		min_delta_ns_ = ns;
	}
	void set_max_delta_ns(int64_t ns) {
		max_delta_ns_ = ns;
	}

	bool has_feature(unsigned int f) const {
		return (features_ & f) != 0;
	}

	// Feature flags
	static constexpr unsigned int kFeaturePeriodic = 0x000001;
	static constexpr unsigned int kFeatureOneShot = 0x000002;
	static constexpr unsigned int kFeaturePerCpu = 0x000040;

	// Convert nanoseconds to device ticks
	int64_t ns_to_ticks(int64_t ns) const {
		return (ns * mult_) >> shift_;
	}

	// Convert device ticks to nanoseconds
	int64_t ticks_to_ns(int64_t ticks) const {
		return (ticks << shift_) / mult_;
	}

	protected:
	State state_ = State::kDetached;
	TimePoint next_event_{0};
	EventHandler event_handler_;
	std::string name_ = "generic";
	int rating_ = 0;
	unsigned int features_ = 0;
	int64_t min_delta_ns_ = 0;
	int64_t max_delta_ns_ = INT64_MAX;
	uint32_t mult_ = 1;
	uint32_t shift_ = 0;
};

//=============================================================================
// SimulatedClockEventDevice — for testing and simulation
//=============================================================================

class SimulatedClockEventDevice : public ClockEventDevice {
	public:
	SimulatedClockEventDevice() {
		set_name("simulated");
		set_features(kFeatureOneShot | kFeaturePeriodic);
	}

	int set_next_event(TimePoint expires) override {
		next_event_ = expires;
		fire_count_++;
		return 0;
	}

	void fire() {
		if (event_handler_) event_handler_();
	}

	uint64_t fire_count() const {
		return fire_count_;
	}

	private:
	uint64_t fire_count_ = 0;
};

//=============================================================================
// ClockManager — manages clock sources and event devices
//=============================================================================

class ClockManager {
	public:
	ClockManager() {
		// Install default monotonic clock source
		current_source_ = std::make_unique<MonotonicClockSource>();
	}

	static ClockManager& instance() {
		static ClockManager mgr;
		return mgr;
	}

	// Clock source management
	void register_source(std::unique_ptr<ClockSource> cs) {
		std::lock_guard<std::mutex> lock(mutex_);
		sources_.push_back(std::move(cs));
		// Re-evaluate best source
		reevaluate_source();
	}

	ClockSource* current_source() {
		std::lock_guard<std::mutex> lock(mutex_);
		return current_source_.get();
	}

	const ClockSource* current_source() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return current_source_.get();
	}

	// Get current time from the active clock source
	TimePoint now() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return current_source_->read();
	}

	TimePoint now_realtime() const {
		return clock_now_realtime();
	}
	TimePoint now_boottime() const {
		return clock_now_boottime();
	}
	TimePoint now_for(ClockId id) const {
		switch (id) {
		case ClockId::kMonotonic:
			return now();
		case ClockId::kRealtime:
			return now_realtime();
		case ClockId::kBoottime:
			return now_boottime();
		case ClockId::kRaw:
			return clock_now_raw();
		default:
			return now();
		}
	}

	// Event device management
	void set_event_device(std::unique_ptr<ClockEventDevice> dev) {
		std::lock_guard<std::mutex> lock(mutex_);
		event_device_ = std::move(dev);
	}

	ClockEventDevice* event_device() {
		std::lock_guard<std::mutex> lock(mutex_);
		return event_device_.get();
	}

	int program_next_event(TimePoint expires) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (event_device_) {
			return event_device_->set_next_event(expires);
		}
		return -1;
	}

	// Timekeeping state
	bool is_suspended() const {
		return suspended_.load();
	}
	void set_suspended(bool s) {
		suspended_.store(s);
	}

	// Time offset tracking (for suspend/resume compensation)
	TimePoint suspend_time() const {
		return suspend_time_;
	}
	void record_suspend(TimePoint t) {
		suspend_time_ = t;
	}

	Duration total_sleep_duration() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return total_sleep_duration_;
	}

	void add_sleep_duration(Duration d) {
		std::lock_guard<std::mutex> lock(mutex_);
		total_sleep_duration_ += d;
	}

	// Inject a sleep offset (for testing or time adjustment)
	void inject_sleep_time(Duration delta) {
		std::lock_guard<std::mutex> lock(mutex_);
		sleep_offset_ += delta;
		total_sleep_duration_ += delta;
	}

	// Resolution of current clock
	int64_t resolution_ns() const {
		// std::chrono::steady_clock typically has ~100ns resolution on Windows,
		// ~1ns on Linux. We report a conservative estimate.
		return 100;	 // nanoseconds
	}

	private:
	void reevaluate_source() {
		// Pick the highest-rated clock source and swap it into
		// current_source_. Using swap avoids transferring ownership
		// from sources_ (which would cause a double-free).
		int best_idx = -1;
		ClockSource* best = current_source_.get();
		for (size_t i = 0; i < sources_.size(); ++i) {
			if (sources_[i]->rating() > best->rating()) {
				best = sources_[i].get();
				best_idx = static_cast<int>(i);
			}
		}
		if (best_idx >= 0) {
			current_source_.swap(sources_[best_idx]);
		}
	}

	mutable std::mutex mutex_;
	std::unique_ptr<ClockSource> current_source_;
	std::vector<std::unique_ptr<ClockSource>> sources_;
	std::unique_ptr<ClockEventDevice> event_device_;
	std::atomic<bool> suspended_{false};
	TimePoint suspend_time_{0};
	Duration sleep_offset_{0};
	Duration total_sleep_duration_{0};
};

}  // namespace engine
