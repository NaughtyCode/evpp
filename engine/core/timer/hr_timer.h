// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// High-resolution timer (hrtimer). Mirrors the Linux kernel hrtimer subsystem
// (kernel/time/hrtimer.c, include/linux/hrtimer.h).
//
// Features:
//  - Nanosecond precision (bounded by OS timer resolution)
//  - Absolute and relative expiry modes
//  - One-shot and repeating timers
//  - Soft and hard priority modes
//  - Range/slack for power-efficient batching
//  - Forwarding for periodic timers
//  - Thread-safe operations

#pragma once

#include "engine/core/timer/timer_core.h"
#include "engine/core/timer/timer_queue.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>

namespace engine {

// Forward declarations
class HrTimerManager;

//=============================================================================
// HrTimerNode — the basic high-resolution timer structure
//=============================================================================

class HrTimerNode {
public:
    friend class HrTimerManager;
    friend class TimerQueueLinked<HrTimerNode>;

    using Callback = std::function<TimerResult(HrTimerNode* timer)>;

    HrTimerNode() = default;

    // Initialize with callback, clock_id, and mode
    void setup(Callback callback, ClockId clock_id, TimerMode mode) {
        callback_ = std::move(callback);
        clock_id_ = clock_id;
        mode_ = mode;
        state_ = TimerState::kInactive;
    }

    ~HrTimerNode() = default;

    // Non-copyable, movable
    HrTimerNode(const HrTimerNode&) = delete;
    HrTimerNode& operator=(const HrTimerNode&) = delete;
    HrTimerNode(HrTimerNode&& other) noexcept
        : expires_(other.expires_.load())
        , softexpires_(other.softexpires_.load())
        , clock_id_(other.clock_id_)
        , mode_(other.mode_)
        , state_(other.state_.load())
        , callback_(std::move(other.callback_))
        , user_data_(other.user_data_)
        , queue_it_(other.queue_it_) {
        other.state_ = TimerState::kInactive;
        other.queue_it_ = MapType::iterator();
    }

    HrTimerNode& operator=(HrTimerNode&& other) noexcept {
        if (this != &other) {
            expires_ = other.expires_.load();
            softexpires_ = other.softexpires_.load();
            clock_id_ = other.clock_id_;
            mode_ = other.mode_;
            state_ = other.state_.load();
            callback_ = std::move(other.callback_);
            user_data_ = other.user_data_;
            queue_it_ = other.queue_it_;
            other.state_ = TimerState::kInactive;
            other.queue_it_ = MapType::iterator();
        }
        return *this;
    }

    //-----------------------------------------------------------------
    // TimerQueueNode interface (used by TimerQueueLinked)
    //-----------------------------------------------------------------

    TimePoint expire_time() const { return expires_.load(); }

    using MapType = std::multimap<TimePoint, HrTimerNode*>;
    using MapIterator = MapType::iterator;

    void set_linked_queue_iterator(MapIterator it) { queue_it_ = it; }
    MapIterator linked_queue_iterator() const { return queue_it_; }
    void clear_linked_queue_iterator() { queue_it_ = MapIterator(); }

    //-----------------------------------------------------------------
    // Accessors
    //-----------------------------------------------------------------

    TimePoint expires()     const { return expires_.load(); }
    TimePoint softexpires() const { return softexpires_.load(); }
    ClockId   clock_id()    const { return clock_id_; }
    TimerMode mode()        const { return mode_; }
    TimerState state()      const { return state_.load(); }
    void*     user_data()   const { return user_data_; }

    bool is_queued() const { return state_.load() == TimerState::kArmed; }
    bool is_absolute() const { return mode_is_absolute(mode_); }
    bool is_relative() const { return mode_is_relative(mode_); }
    bool is_repeating() const { return mode_is_repeating(mode_); }
    bool is_one_shot() const { return mode_is_one_shot(mode_); }
    bool is_soft() const { return mode_is_soft(mode_); }
    bool is_hard() const { return mode_is_hard(mode_); }

    void set_user_data(void* data) { user_data_ = data; }
    void set_callback(Callback cb) { callback_ = std::move(cb); }

    //-----------------------------------------------------------------
    // Expiry time manipulation (mirror hrtimer_set_expires etc.)
    //-----------------------------------------------------------------

    void set_expires(TimePoint time) {
        expires_.store(time);
        softexpires_.store(time);
    }

    void set_expires_range(TimePoint time, Duration delta) {
        softexpires_.store(time);
        expires_.store(time_add_safe(time, delta));
    }

    void set_expires_range_ns(TimePoint time, int64_t delta_ns) {
        softexpires_.store(time);
        expires_.store(time_add_safe(time, Duration(delta_ns)));
    }

    void add_expires(TimePoint time) {
        expires_.store(time_add_safe(expires_.load(), time));
        softexpires_.store(time_add_safe(softexpires_.load(), time));
    }

    void add_expires_ns(int64_t ns) {
        expires_.store(time_add_ns(expires_.load(), ns));
        softexpires_.store(time_add_ns(softexpires_.load(), ns));
    }

    // Get remaining time until expiry
    TimePoint remaining(TimePoint now) const {
        return time_sub(expires_.load(), now);
    }

    // Forward the timer by interval until it expires after 'now'
    // Returns the number of overruns (mirror hrtimer_forward)
    int64_t forward(TimePoint now, Duration interval) {
        if (interval.count() <= 0) return 0;

        TimePoint exp = expires_.load();
        if (time_compare(exp, now) >= 0) return 0;

        int64_t delta_ns = time_delta_ns(now, exp);
        int64_t interval_ns = interval.count();
        int64_t overruns = delta_ns / interval_ns + 1;

        // Advance by overruns * interval
        Duration advance = Duration(overruns * interval_ns);
        expires_.store(time_add_safe(exp, advance));
        softexpires_.store(time_add_safe(softexpires_.load(), advance));

        return overruns;
    }

private:
    std::atomic<TimePoint> expires_{kTimeMax};
    std::atomic<TimePoint> softexpires_{kTimeMax};
    ClockId   clock_id_ = ClockId::kMonotonic;
    TimerMode mode_ = TimerMode::kDefault;
    std::atomic<TimerState> state_{TimerState::kInactive};
    Callback   callback_;
    void*      user_data_ = nullptr;

    // Queue iterator for O(log n) removal
    MapIterator queue_it_;
};

//=============================================================================
// HrTimerSleeper — timer + task coordination (mirror hrtimer_sleeper)
//=============================================================================

class HrTimerSleeper {
public:
    HrTimerSleeper() = default;

    void setup(ClockId clock_id, TimerMode mode) {
        timer_.setup(nullptr, clock_id, mode);
    }

    HrTimerNode& timer()       { return timer_; }
    const HrTimerNode& timer() const { return timer_; }
    bool is_sleeping() const   { return sleeping_.load(); }

    void set_sleeping(bool s)  { sleeping_.store(s); }
    void wake()                { sleeping_.store(false); }

private:
    HrTimerNode timer_;
    std::atomic<bool> sleeping_{false};
};

//=============================================================================
// HrTimerManager — manages a collection of high-resolution timers
// (mirrors hrtimer_cpu_base / hrtimer_clock_base in Linux)
//=============================================================================

class HrTimerManager {
public:
    HrTimerManager() = default;
    ~HrTimerManager() = default;

    // Non-copyable
    HrTimerManager(const HrTimerManager&) = delete;
    HrTimerManager& operator=(const HrTimerManager&) = delete;

    //-----------------------------------------------------------------
    // Timer setup
    //-----------------------------------------------------------------

    void setup_timer(HrTimerNode* timer, HrTimerNode::Callback callback,
                     ClockId clock_id, TimerMode mode) {
        assert(timer);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        timer->setup(std::move(callback), clock_id, mode);
    }

    void setup_timer_on_stack(HrTimerNode* timer, HrTimerNode::Callback callback,
                              ClockId clock_id, TimerMode mode) {
        setup_timer(timer, std::move(callback), clock_id, mode);
    }

    //-----------------------------------------------------------------
    // Start / restart timers
    //-----------------------------------------------------------------

    // Start with nanosecond precision range (mirror hrtimer_start_range_ns)
    void start_range_ns(HrTimerNode* timer, TimePoint expiry_time,
                        int64_t range_ns, TimerMode mode) {
        assert(timer);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        // Remove if already queued
        if (timer->is_queued()) {
            remove_locked(timer);
        }

        TimePoint now = get_now_for(timer->clock_id());

        // If relative, adjust to absolute
        if (mode_is_relative(mode)) {
            expiry_time = time_add_safe(now, expiry_time);
        }

        timer->set_expires_range_ns(expiry_time, range_ns);
        timer->mode_ = mode;
        timer->state_ = TimerState::kArmed;
        queue_.add(timer);
        stats_.record_arm();
    }

    // Start a timer (mirror hrtimer_start)
    void start(HrTimerNode* timer, TimePoint expiry_time, TimerMode mode) {
        start_range_ns(timer, expiry_time, 0, mode);
    }

    // Restart with current expires values (mirror hrtimer_start_expires)
    void start_expires(HrTimerNode* timer, TimerMode mode) {
        TimePoint soft = timer->softexpires();
        TimePoint hard = timer->expires();
        int64_t delta_ns = time_delta_ns(hard, soft);
        start_range_ns(timer, soft, delta_ns, mode);
    }

    void restart(HrTimerNode* timer) {
        start_expires(timer, TimerMode::kAbsolute | (timer->mode() & TimerMode::kHard));
    }

    //-----------------------------------------------------------------
    // Cancel timers
    //-----------------------------------------------------------------

    bool cancel(HrTimerNode* timer) {
        assert(timer);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (!timer->is_queued()) return false;

        remove_locked(timer);
        timer->state_ = TimerState::kCancelled;
        stats_.record_cancel();
        return true;
    }

    bool try_to_cancel(HrTimerNode* timer) {
        assert(timer);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (timer->state_.load() == TimerState::kFiring) return false;
        return cancel_locked(timer);
    }

    //-----------------------------------------------------------------
    // Query timers
    //-----------------------------------------------------------------

    TimePoint get_remaining(HrTimerNode* timer, TimePoint now) const {
        assert(timer);
        if (!timer->is_queued()) return kTimeMax;
        return timer->remaining(now);
    }

    bool is_active(HrTimerNode* timer) const {
        return timer->is_queued();
    }

    // Get the expiration time of the next timer to fire
    TimePoint next_expiry() const {
        return queue_.first_expiry();
    }

    // Get the next event time (returns 0 if no timers pending)
    int64_t next_event_ns(TimePoint now) const {
        TimePoint next = next_expiry();
        if (next == kTimeMax) return INT64_MAX;
        int64_t remaining = time_delta_ns(next, now);
        return remaining > 0 ? remaining : 0;
    }

    // Next expiry excluding a specific timer
    TimePoint next_expiry_without(const HrTimerNode* exclude) const {
        auto* next = queue_.next_expiring(TimePoint::min(), const_cast<HrTimerNode*>(exclude));
        return next ? next->expires() : kTimeMax;
    }

    // Forward a repeating timer
    int64_t forward(HrTimerNode* timer, TimePoint now, Duration interval) {
        assert(timer);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return timer->forward(now, interval);
    }

    int64_t forward_now(HrTimerNode* timer, Duration interval) {
        TimePoint now = get_now_for(timer->clock_id());
        return forward(timer, now, interval);
    }

    //-----------------------------------------------------------------
    // Timer expiration processing (call from main loop)
    //-----------------------------------------------------------------

    // Process all expired timers, calling their callbacks.
    // Returns the number of timers fired.
    size_t process_expired(TimePoint now, size_t max_to_process = SIZE_MAX) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        size_t processed = 0;

        while (!queue_.empty() && processed < max_to_process) {
            HrTimerNode* timer = queue_.top();
            if (timer->expires() > now) break;

            // Remove and mark as firing
            queue_.remove(timer);
            timer->state_ = TimerState::kFiring;

            // Unlock during callback to avoid deadlocks
            TimerResult result;
            {
                // Temporarily release the lock for callback
                mutex_.unlock();
                int64_t latency = time_delta_ns(now, timer->expires());
                stats_.record_expire(latency);

                result = timer->callback_ ? timer->callback_(timer) : TimerResult::kNoRestart;

                mutex_.lock();
            }

            processed++;

            // Handle restart request
            if (result == TimerResult::kRestart) {
                stats_.record_restart();
                if (timer->is_repeating() && !timer->is_queued()) {
                    // Auto-re-arm: callback didn't already start() this timer
                    timer->state_ = TimerState::kArmed;
                    queue_.add(timer);
                } else if (!timer->is_repeating() && !timer->is_queued()) {
                    // Non-repeating timer: callback didn't re-arm, so mark inactive
                    timer->state_ = TimerState::kInactive;
                }
                // else: timer was re-armed by callback — leave state as-is
            } else if (result == TimerResult::kNoRestart) {
                timer->state_ = TimerState::kInactive;
            }
        }

        return processed;
    }

    // Process all expired timers (convenience, runs until queue is current)
    size_t process_all_expired(TimePoint now) {
        return process_expired(now);
    }

    //-----------------------------------------------------------------
    // Sleep helpers
    //-----------------------------------------------------------------

    // Sleep for the specified duration or until timeout
    int sleep_timeout_range(TimePoint& expires, int64_t delta_ns, TimerMode mode) {
        HrTimerSleeper sleeper;
        sleeper.setup(ClockId::kMonotonic, mode | TimerMode::kAbsolute);
        sleeper.set_sleeping(true);

        start_range_ns(&sleeper.timer(), expires, delta_ns, mode);

        // Wait for the timer to fire
        while (sleeper.is_sleeping()) {
            TimePoint now = clock_now_for(ClockId::kMonotonic);
            if (now >= sleeper.timer().expires()) {
                sleeper.wake();
                cancel(&sleeper.timer());
                return 0;
            }

            // Sleep in small increments for responsiveness
            int64_t remaining = time_delta_ns(sleeper.timer().expires(), now);
            if (remaining > 1'000'000) { // 1ms
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (remaining > 0) {
                std::this_thread::yield();
            }
        }
        return 0;
    }

    int sleep_timeout(TimePoint& expires, TimerMode mode) {
        return sleep_timeout_range(expires, 0, mode);
    }

    //-----------------------------------------------------------------
    // Statistics
    //-----------------------------------------------------------------

    const TimerStats& stats() const { return stats_; }
    size_t active_count() const { return queue_.size(); }
    void reset_stats()  { stats_ = TimerStats{}; }

    //-----------------------------------------------------------------
    // Debug
    //-----------------------------------------------------------------

    void show_timers(std::function<void(HrTimerNode*)> visitor) const {
        queue_.for_each([&](HrTimerNode* node) { visitor(node); });
    }

private:
    TimePoint get_now_for(ClockId id) const {
        return clock_now_for(id);
    }

    void remove_locked(HrTimerNode* timer) {
        queue_.remove(timer);
    }

    bool cancel_locked(HrTimerNode* timer) {
        if (!timer->is_queued()) return false;
        remove_locked(timer);
        timer->state_ = TimerState::kCancelled;
        stats_.record_cancel();
        return true;
    }

    TimerQueueLinked<HrTimerNode> queue_;
    TimerStats stats_;
    mutable std::recursive_mutex mutex_;
};

} // namespace engine
