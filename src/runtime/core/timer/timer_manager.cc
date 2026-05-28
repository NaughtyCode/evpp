// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// TimerManager implementation.

#include "runtime/core/timer/timer_manager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <unordered_map>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {

//=============================================================================
// Construction / Destruction
//=============================================================================

TimerManager::TimerManager()
	: hrtimer_mgr_(std::make_unique<HrTimerManager>()),
	  wheel_(std::make_unique<TimerWheel>()),
	  alarm_mgr_(std::make_unique<AlarmTimerManager>()) {
}

TimerManager::~TimerManager() {
	shutdown();
}

//=============================================================================
// Initialization / shutdown
//=============================================================================

void TimerManager::initialize() {
	if (initialized_.exchange(true)) return;
	last_update_time_ = now();
}

void TimerManager::shutdown() {
	if (!initialized_.exchange(false)) return;

	{
		std::lock_guard<std::mutex> lock(entries_mutex_);
		for (auto& [id, entry] : entries_) {
			switch (entry->kind) {
			case TimerEntry::Kind::kHrTimer:
				hrtimer_mgr_->cancel(entry->hrtimer);
				break;
			case TimerEntry::Kind::kWheelTimer:
				wheel_->del_timer(entry->wheel_timer);
				break;
			case TimerEntry::Kind::kAlarm:
				alarm_mgr_->cancel(entry->alarm);
				break;
			}
		}
		entries_.clear();
	}
}

//=============================================================================
// Main loop update
//=============================================================================

TimerManager::UpdateResult TimerManager::update() {
	return update(now());
}

TimerManager::UpdateResult TimerManager::update(TimePoint current_time) {
	ENGINE_PROFILE_SCOPE("engine.timer", "Update");

	UpdateResult result;
	result.current_time = current_time;
	result.elapsed = current_time - last_update_time_;
	last_update_time_ = current_time;

	auto t_start = std::chrono::steady_clock::now();

	// Process all timer subsystems
	result = update_hrtimers(result, current_time);
	result = update_wheel(result);
	result = update_alarms(result, current_time);

	// Calculate next event time (minimum across all subsystems)
	int64_t next_hr = hrtimer_mgr_->next_event_ns(current_time);
	if (next_hr < 0) next_hr = 0;

	int64_t next_wheel = wheel_->next_expiry_ms();
	if (next_wheel < INT64_MAX) {
		int64_t wheel_ns = (next_wheel - wheel_->current_jiffy()) * kNsPerMs;
		if (wheel_ns < 0) wheel_ns = 0;
		if (wheel_ns < next_hr) next_hr = wheel_ns;
	}

	TimePoint alarm_next = alarm_mgr_->next_expiry();
	if (alarm_next != kTimeMax) {
		int64_t alarm_ns = time_delta_ns(alarm_next, current_time);
		if (alarm_ns < 0) alarm_ns = 0;
		if (alarm_ns < next_hr) next_hr = alarm_ns;
	}

	result.next_event_ns = next_hr;

	result.total_fired = result.hrtimers_fired + result.wheel_timers_fired + result.alarms_fired;

	if (result.total_fired > 0) {
		ENGINE_PROFILE_INSTANT("engine.timer", "TimerFired");
	}

	// Update statistics
	stats_.total_updates++;
	auto t_end = std::chrono::steady_clock::now();
	total_update_time_ns_ +=
		std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();
	stats_.avg_update_time_us =
		static_cast<int64_t>(total_update_time_ns_ / stats_.total_updates / 1000);

	return result;
}

TimerManager::UpdateResult TimerManager::update_hrtimers(UpdateResult result, TimePoint now) {
	result.hrtimers_fired = hrtimer_mgr_->process_all_expired(now);
	stats_.hrtimer_stats = hrtimer_mgr_->stats();
	return result;
}

TimerManager::UpdateResult TimerManager::update_wheel(UpdateResult result) {
	// Advance wheel by elapsed jiffies
	if (result.elapsed.count() > 0) {
		int64_t elapsed_ms = time_to_ms(result.elapsed);
		if (elapsed_ms < 1) elapsed_ms = 1;

		auto expired = wheel_->advance(elapsed_ms);
		for (auto* node : expired) {
			node->fire();
		}
		result.wheel_timers_fired = expired.size();
	}
	stats_.wheel_stats = wheel_->stats();
	return result;
}

TimerManager::UpdateResult TimerManager::update_alarms(UpdateResult result, TimePoint now) {
	auto fired = alarm_mgr_->process_expired(now);
	result.alarms_fired = fired.size();
	return result;
}

//=============================================================================
// Timer ID management
//=============================================================================

TimerId TimerManager::allocate_id() {
	return next_id_.fetch_add(1, std::memory_order_relaxed);
}

void TimerManager::free_id(TimerId id) {
	std::lock_guard<std::mutex> lock(entries_mutex_);
	entries_.erase(id);
}

TimerManager::TimerEntry* TimerManager::get_entry(TimerId id) {
	std::lock_guard<std::mutex> lock(entries_mutex_);
	auto it = entries_.find(id);
	return (it != entries_.end()) ? it->second.get() : nullptr;
}

const TimerManager::TimerEntry* TimerManager::get_entry(TimerId id) const {
	std::lock_guard<std::mutex> lock(entries_mutex_);
	auto it = entries_.find(id);
	return (it != entries_.end()) ? it->second.get() : nullptr;
}

//=============================================================================
// Time query API
//=============================================================================

TimePoint TimerManager::now() const {
	return clock_mgr_.now();
}

TimePoint TimerManager::now_real() const {
	return clock_mgr_.now_realtime();
}

TimePoint TimerManager::now_boottime() const {
	return clock_mgr_.now_boottime();
}

TimePoint TimerManager::now_tai() const {
	return clock_now_realtime();  // TAI approx via realtime
}

TimePoint TimerManager::now_raw() const {
	return clock_now_raw();
}

int64_t TimerManager::now_ns() const {
	return time_to_ns(now());
}

int64_t TimerManager::now_real_ns() const {
	return time_to_ns(now_real());
}

int64_t TimerManager::now_boottime_ns() const {
	return time_to_ns(now_boottime());
}

TimePoint TimerManager::now_coarse() const {
	// Coarse = millisecond precision
	int64_t ns = time_to_ns(now());
	return TimePoint((ns / kNsPerMs) * kNsPerMs);
}

int64_t TimerManager::now_coarse_ns() const {
	return time_to_ns(now_coarse());
}

int64_t TimerManager::now_fast_ns() const {
	// Lock-free fast path
	return time_to_ns(clock_mgr_.now());
}

int64_t TimerManager::resolution_ns() const {
	return clock_mgr_.resolution_ns();
}

//=============================================================================
// High-Resolution Timer API
//=============================================================================

TimerId TimerManager::create_timer(HrTimerNode::Callback callback,
								   ClockId clock_id,
								   TimerMode mode) {
	auto entry = std::make_unique<TimerEntry>();
	entry->kind = TimerEntry::Kind::kHrTimer;
	entry->hrtimer = MEM_NEW(HrTimerNode);
	entry->hrtimer->setup(std::move(callback), clock_id, mode);

	TimerId id = allocate_id();
	{
		std::lock_guard<std::mutex> lock(entries_mutex_);
		entries_[id] = std::move(entry);
	}
	return id;
}

TimerId TimerManager::create_simple_timer(std::function<void()> callback, ClockId clock_id) {
	auto simple_cb = std::make_shared<std::function<void()>>(std::move(callback));

	auto hr_cb = [simple_cb](HrTimerNode* /*timer*/) -> TimerResult {
		(*simple_cb)();
		return TimerResult::kNoRestart;
	};

	return create_timer(std::move(hr_cb), clock_id, TimerMode::kDefault);
}

void TimerManager::start_timer(TimerId id, TimePoint expiry, TimerMode mode) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return;
	ENGINE_PROFILE_INSTANT("engine.timer", "StartTimer");
	hrtimer_mgr_->start(entry->hrtimer, expiry, mode);
}

void TimerManager::start_timer_relative(TimerId id, Duration relative_time, TimerMode mode) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return;
	TimePoint expiry = time_add(now(), relative_time);
	// Start from the timer's stored mode (preserves kSoft/kHard/kPinned/kDeferrable/kRepeating).
	// Only override the absolute/relative bit: we've already computed absolute expiry.
	TimerMode new_mode = entry->hrtimer->mode();
	new_mode = static_cast<TimerMode>(static_cast<uint32_t>(new_mode) &
									  ~static_cast<uint32_t>(TimerMode::kRelative));
	new_mode = new_mode | TimerMode::kAbsolute;
	hrtimer_mgr_->start(entry->hrtimer, expiry, new_mode);
}

void TimerManager::start_timer_range(TimerId id,
									 TimePoint expiry,
									 int64_t range_ns,
									 TimerMode mode) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return;
	hrtimer_mgr_->start_range_ns(entry->hrtimer, expiry, range_ns, mode);
}

bool TimerManager::cancel_timer(TimerId id) {
	auto* entry = get_entry(id);
	if (!entry) return false;

	bool result = false;
	switch (entry->kind) {
	case TimerEntry::Kind::kHrTimer:
		result = hrtimer_mgr_->cancel(entry->hrtimer);
		break;
	case TimerEntry::Kind::kWheelTimer:
		result = wheel_->del_timer(entry->wheel_timer);
		break;
	case TimerEntry::Kind::kAlarm:
		result = alarm_mgr_->cancel(entry->alarm);
		break;
	}
	if (result) ENGINE_PROFILE_INSTANT("engine.timer", "CancelTimer");
	return result;
}

void TimerManager::restart_timer(TimerId id) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return;
	hrtimer_mgr_->restart(entry->hrtimer);
}

int64_t TimerManager::forward_timer(TimerId id, Duration interval) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return 0;
	return hrtimer_mgr_->forward_now(entry->hrtimer, interval);
}

bool TimerManager::is_timer_active(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry) return false;
	switch (entry->kind) {
	case TimerEntry::Kind::kHrTimer:
		return hrtimer_mgr_->is_active(entry->hrtimer);
	case TimerEntry::Kind::kWheelTimer:
		return wheel_->timer_pending(entry->wheel_timer);
	case TimerEntry::Kind::kAlarm:
		return entry->alarm->is_armed();
	}
	return false;
}

Duration TimerManager::timer_remaining(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry) return Duration::max();
	switch (entry->kind) {
	case TimerEntry::Kind::kHrTimer:
		return Duration(hrtimer_mgr_->get_remaining(entry->hrtimer, now()).count());
	case TimerEntry::Kind::kWheelTimer:
		if (!wheel_->timer_pending(entry->wheel_timer)) return Duration::max();
		return time_sub(entry->wheel_timer->expires(), now());
	case TimerEntry::Kind::kAlarm:
		return alarm_mgr_->expires_remaining(entry->alarm);
	}
	return Duration::max();
}

TimePoint TimerManager::timer_expires(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry) return kTimeMax;
	switch (entry->kind) {
	case TimerEntry::Kind::kHrTimer:
		return entry->hrtimer->expires();
	case TimerEntry::Kind::kWheelTimer:
		return entry->wheel_timer->expires();
	case TimerEntry::Kind::kAlarm:
		return entry->alarm->expires();
	}
	return kTimeMax;
}

TimerState TimerManager::timer_state(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry) return TimerState::kInactive;
	switch (entry->kind) {
	case TimerEntry::Kind::kHrTimer:
		return entry->hrtimer->state();
	case TimerEntry::Kind::kWheelTimer:
		return entry->wheel_timer->state();
	case TimerEntry::Kind::kAlarm:
		return entry->alarm->is_armed() ? TimerState::kArmed : TimerState::kInactive;
	}
	return TimerState::kInactive;
}

void TimerManager::destroy_timer(TimerId id) {
	cancel_timer(id);

	std::lock_guard<std::mutex> lock(entries_mutex_);
	auto it = entries_.find(id);
	if (it != entries_.end()) {
		entries_.erase(it);
	}
}

void TimerManager::set_timer_callback(TimerId id, HrTimerNode::Callback callback) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kHrTimer) return;
	entry->hrtimer->set_callback(std::move(callback));
}

//=============================================================================
// Timer Wheel API
//=============================================================================

TimerId TimerManager::create_wheel_timer(TimerWheelNode::Callback callback, uint32_t flags) {
	auto entry = std::make_unique<TimerEntry>();
	entry->kind = TimerEntry::Kind::kWheelTimer;
	entry->wheel_timer = MEM_NEW(TimerWheelNode);
	entry->wheel_timer->setup(std::move(callback), flags);

	TimerId id = allocate_id();
	{
		std::lock_guard<std::mutex> lock(entries_mutex_);
		entries_[id] = std::move(entry);
	}
	return id;
}

void TimerManager::start_wheel_timer(TimerId id, int64_t expires_ms) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kWheelTimer) return;
	ENGINE_PROFILE_INSTANT("engine.timer", "StartWheelTimer");
	wheel_->add_timer(entry->wheel_timer, expires_ms);
}

void TimerManager::mod_wheel_timer(TimerId id, int64_t expires_ms) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kWheelTimer) return;
	wheel_->mod_timer(entry->wheel_timer, expires_ms);
}

bool TimerManager::cancel_wheel_timer(TimerId id) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kWheelTimer) return false;
	return wheel_->del_timer(entry->wheel_timer);
}

bool TimerManager::wheel_timer_pending(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kWheelTimer) return false;
	return wheel_->timer_pending(entry->wheel_timer);
}

//=============================================================================
// Alarm Timer API
//=============================================================================

TimerId TimerManager::create_alarm(AlarmType type, Alarm::Callback callback) {
	auto entry = std::make_unique<TimerEntry>();
	entry->kind = TimerEntry::Kind::kAlarm;
	entry->alarm = MEM_NEW(Alarm);
	entry->alarm->init(type, std::move(callback));

	TimerId id = allocate_id();
	{
		std::lock_guard<std::mutex> lock(entries_mutex_);
		entries_[id] = std::move(entry);
	}
	return id;
}

void TimerManager::start_alarm(TimerId id, TimePoint start_time) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return;
	ENGINE_PROFILE_INSTANT("engine.timer", "StartAlarm");
	alarm_mgr_->start(entry->alarm, start_time);
}

void TimerManager::start_alarm_relative(TimerId id, Duration relative_time) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return;
	alarm_mgr_->start_relative(entry->alarm, relative_time);
}

bool TimerManager::cancel_alarm(TimerId id) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return false;
	return alarm_mgr_->cancel(entry->alarm);
}

Duration TimerManager::alarm_remaining(TimerId id) const {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return Duration::max();
	return alarm_mgr_->expires_remaining(entry->alarm);
}

int64_t TimerManager::forward_alarm(TimerId id, Duration interval) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return 0;
	return alarm_mgr_->forward_now(entry->alarm, interval);
}

void TimerManager::restart_alarm(TimerId id) {
	auto* entry = get_entry(id);
	if (!entry || entry->kind != TimerEntry::Kind::kAlarm) return;
	alarm_mgr_->restart(entry->alarm);
}

//=============================================================================
// Repeating timer creation
//=============================================================================

TimerId TimerManager::create_repeating_timer(Duration interval,
											 HrTimerNode::Callback callback,
											 ClockId clock_id) {
	auto hr_cb = [callback = std::move(callback), mgr = hrtimer_mgr_.get(),
				  interval](HrTimerNode* timer) mutable -> TimerResult {
		TimerResult result = callback(timer);
		if (result == TimerResult::kRestart) {
			timer->add_expires(interval);
			mgr->start(timer, timer->expires(), timer->mode());
		}
		return result;
	};

	return create_timer(hr_cb, clock_id, TimerMode::kAbsolute | TimerMode::kRepeating);
}

TimerId TimerManager::create_repeating_simple_timer(Duration interval,
													std::function<void()> callback,
													ClockId clock_id) {
	auto cb = std::make_shared<std::function<void()>>(std::move(callback));
	auto hr_cb = [cb, interval, mgr = hrtimer_mgr_.get()](HrTimerNode* timer) -> TimerResult {
		(*cb)();
		if (timer->state() != TimerState::kCancelled) {
			timer->add_expires(interval);
			mgr->start(timer, timer->expires(), timer->mode());
		}
		return TimerResult::kRestart;
	};

	return create_timer(std::move(hr_cb), clock_id, TimerMode::kAbsolute | TimerMode::kRepeating);
}

//=============================================================================
// Suspend / Resume
//=============================================================================

void TimerManager::on_suspend() {
	suspended_.store(true);
	suspend_start_ = now();
	alarm_mgr_->on_suspend();
}

void TimerManager::on_resume() {
	if (!suspended_.load()) return;
	suspended_.store(false);

	TimePoint resume_time = now();
	Duration sleep_dur = resume_time - suspend_start_;
	suspend_offset_ += sleep_dur;
	alarm_mgr_->on_resume();
}

bool TimerManager::is_suspended() const {
	return suspended_.load();
}

Duration TimerManager::total_suspend_duration() const {
	return suspend_offset_;
}

//=============================================================================
// Time adjustment
//=============================================================================

void TimerManager::inject_sleep_time(Duration delta) {
	clock_mgr_.inject_sleep_time(delta);
}

void TimerManager::set_time_scale(double scale) {
	time_scale_.store(std::max(0.0, scale));
}

double TimerManager::time_scale() const {
	return time_scale_.load();
}

//=============================================================================
// Clock source
//=============================================================================

ClockManager& TimerManager::clock_manager() {
	return clock_mgr_;
}

const ClockManager& TimerManager::clock_manager() const {
	return clock_mgr_;
}

void TimerManager::set_clock_source(std::unique_ptr<ClockSource> cs) {
	clock_mgr_.register_source(std::move(cs));
}

//=============================================================================
// Statistics
//=============================================================================

TimerManager::ManagerStats TimerManager::stats() const {
	ManagerStats s = stats_;
	s.hrtimer_stats = hrtimer_mgr_->stats();
	s.wheel_stats = wheel_->stats();
	s.active_hrtimers = hrtimer_mgr_->active_count();
	s.active_wheel_timers = wheel_->active_count();
	s.active_alarms = alarm_mgr_->pending_count();
	s.time_scale = time_scale_.load();
	s.suspended = suspended_.load();
	s.total_suspend_dur = suspend_offset_;

	// Count total entries
	{
		std::lock_guard<std::mutex> lock(entries_mutex_);
		for (const auto& [id, entry] : entries_) {
			switch (entry->kind) {
			case TimerEntry::Kind::kHrTimer:
				s.total_hrtimers++;
				break;
			case TimerEntry::Kind::kWheelTimer:
				s.total_wheel_timers++;
				break;
			case TimerEntry::Kind::kAlarm:
				s.total_alarms++;
				break;
			}
		}
	}
	return s;
}

void TimerManager::reset_stats() {
	stats_ = ManagerStats{};
	hrtimer_mgr_->reset_stats();
	wheel_->reset_stats();
	alarm_mgr_->reset_stats();
}

std::vector<TimerId> TimerManager::active_timers() const {
	std::vector<TimerId> result;
	std::lock_guard<std::mutex> lock(entries_mutex_);
	for (const auto& [id, entry] : entries_) {
		bool active = false;
		switch (entry->kind) {
		case TimerEntry::Kind::kHrTimer:
			active = hrtimer_mgr_->is_active(entry->hrtimer);
			break;
		case TimerEntry::Kind::kWheelTimer:
			active = wheel_->timer_pending(entry->wheel_timer);
			break;
		case TimerEntry::Kind::kAlarm:
			active = entry->alarm->is_armed();
			break;
		}
		if (active) result.push_back(id);
	}
	return result;
}

	void TimerManager::dump_state() const {
		auto s = stats();
		ENGINE_LOG_INFO(GetLogger(),
			"=== GameTimerLib State Dump ===\n"
			"  Total updates:       {}\n"
			"  Avg update time:     {} us\n"
			"  Time scale:          {}\n"
			"  Suspended:           {}\n"
			"  HRTimers (active):   {} / {}\n"
			"  Wheel timers:        {} / {}\n"
			"  Alarms (active):     {} / {}\n"
			"  HRTimer fired:       {}\n"
			"  Wheel fired:         {}\n"
			"  HRTimer avg latency: {} us\n"
			"  Wheel avg latency:   {} us\n"
			"  Next HR expiry:      {} ns from epoch\n"
			"  Next wheel expiry:   {} ms jiffy\n"
			"  Clock source:        {}\n"
			"================================",
			s.total_updates,
			s.avg_update_time_us,
			s.time_scale,
			(s.suspended ? "yes" : "no"),
			s.active_hrtimers, s.total_hrtimers,
			s.active_wheel_timers, s.total_wheel_timers,
			s.active_alarms, s.total_alarms,
			s.hrtimer_stats.total_expired,
			s.wheel_stats.total_expired,
			s.hrtimer_stats.avg_latency_us(),
			s.wheel_stats.avg_latency_us(),
			hrtimer_mgr_->next_expiry().count(),
			wheel_->next_expiry_ms(),
			clock_mgr_.current_source()->name());
	}

}  // namespace engine
