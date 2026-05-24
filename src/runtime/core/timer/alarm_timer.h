// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// Alarm timers — timers that survive system suspend / game pause states.
// Mirrors the Linux kernel alarmtimer subsystem
// (kernel/time/alarmtimer.c, include/linux/alarmtimer.h).
//
// In a game engine context, "suspend" might mean:
//  - The game was paused (menu opened)
//  - The app was backgrounded on mobile
//  - The system entered sleep/suspend
//
// Alarm timers track time against a clock that includes suspend time
// (e.g., CLOCK_BOOTTIME or CLOCK_REALTIME), so they correctly handle
// pause/resume cycles.

#pragma once

#include "core/timer/timer_core.h"
#include "core/timer/timer_queue.h"

#include <cassert>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace engine {

// Forward declarations
class AlarmTimerManager;

//=============================================================================
// AlarmType — which clock the alarm is based on
//=============================================================================

enum class AlarmType {
    kRealtime,  // CLOCK_REALTIME based
    kBoottime,  // CLOCK_BOOTTIME based (monotonic + suspend)
};

//=============================================================================
// Alarm — the alarm timer structure
//=============================================================================

class Alarm {
    friend class AlarmTimerManager;
public:
    using Callback = std::function<void(Alarm* alarm, TimePoint now)>;
    using MapType = std::multimap<TimePoint, Alarm*>;
    using MapIterator = MapType::iterator;

    Alarm() = default;

    void init(AlarmType type, Callback callback) {
        type_ = type;
        callback_ = std::move(callback);
        state_ = kStateInactive;
    }

    ~Alarm() = default;

    Alarm(const Alarm&) = delete;
    Alarm& operator=(const Alarm&) = delete;
    Alarm(Alarm&& other) noexcept
        : expires_(other.expires_)
        , type_(other.type_)
        , state_(other.state_)
        , callback_(std::move(other.callback_))
        , data_(other.data_)
        , queue_it_(other.queue_it_) {
        other.state_ = kStateInactive;
        other.queue_it_ = MapIterator();
    }

    Alarm& operator=(Alarm&& other) noexcept {
        if (this != &other) {
            expires_ = other.expires_;
            type_ = other.type_;
            state_ = other.state_;
            callback_ = std::move(other.callback_);
            data_ = other.data_;
            queue_it_ = other.queue_it_;
            other.state_ = kStateInactive;
            other.queue_it_ = MapIterator();
        }
        return *this;
    }

    // Accessors
    TimePoint expires()   const { return expires_; }
    AlarmType type()      const { return type_; }
    bool      is_armed()  const { return state_ == kStateEnqueued; }
    void*     data()      const { return data_; }
    void      set_data(void* d) { data_ = d; }

    // For TimerQueue compatibility
    TimePoint expire_time() const { return expires_; }
    void set_queue_iterator(MapIterator it) { queue_it_ = it; }
    MapIterator queue_iterator() const { return queue_it_; }
    void clear_queue_iterator() { queue_it_ = MapIterator(); }

    // Called when the alarm fires
    void fire(TimePoint now) {
        state_ = kStateFiring;
        if (callback_) {
            callback_(this, now);
        }
        if (state_ == kStateFiring) {
            state_ = kStateInactive;
        }
    }

private:
    static constexpr int kStateInactive = 0x00;
    static constexpr int kStateEnqueued = 0x01;
    static constexpr int kStateFiring   = 0x02;

    TimePoint expires_{0};
    AlarmType type_ = AlarmType::kBoottime;
    int       state_ = kStateInactive;
    Callback  callback_;
    void*     data_ = nullptr;
    MapIterator queue_it_;
};

//=============================================================================
// AlarmTimerManager — manages alarm timers
//=============================================================================

class AlarmTimerManager {
public:
    AlarmTimerManager() = default;
    ~AlarmTimerManager() = default;

    AlarmTimerManager(const AlarmTimerManager&) = delete;
    AlarmTimerManager& operator=(const AlarmTimerManager&) = delete;

    //-----------------------------------------------------------------
    // Alarm setup
    //-----------------------------------------------------------------

    void init_alarm(Alarm* alarm, AlarmType type, Alarm::Callback callback) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        alarm->init(type, std::move(callback));
    }

    //-----------------------------------------------------------------
    // Start alarms
    //-----------------------------------------------------------------

    void start(Alarm* alarm, TimePoint start_time) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (alarm->is_armed()) {
            remove_locked(alarm);
        }

        alarm->expires_ = start_time;
        alarm->state_ = Alarm::kStateEnqueued;
        queue_.add(alarm);
        stats_.record_arm();
    }

    void start_relative(Alarm* alarm, Duration relative_time) {
        TimePoint now = get_now_for(alarm->type());
        start(alarm, time_add(now, relative_time));
    }

    void restart(Alarm* alarm) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // For repeating alarms, the callback should re-arm with start()
        if (alarm->is_armed()) {
            remove_locked(alarm);
        }
        alarm->state_ = Alarm::kStateEnqueued;
        queue_.add(alarm);
        stats_.record_arm();
    }

    //-----------------------------------------------------------------
    // Cancel alarms
    //-----------------------------------------------------------------

    bool try_to_cancel(Alarm* alarm) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (alarm->state_ == Alarm::kStateFiring) return false;
        if (!alarm->is_armed()) return false;
        remove_locked(alarm);
        alarm->state_ = Alarm::kStateInactive;
        stats_.record_cancel();
        return true;
    }

    bool cancel(Alarm* alarm) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!alarm->is_armed()) return false;
        remove_locked(alarm);
        alarm->state_ = Alarm::kStateInactive;
        stats_.record_cancel();
        return true;
    }

    //-----------------------------------------------------------------
    // Forward a repeating alarm
    //-----------------------------------------------------------------

    int64_t forward(Alarm* alarm, TimePoint now, Duration interval) {
        assert(alarm);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (interval.count() <= 0) return 0;

        TimePoint exp = alarm->expires_;
        if (time_compare(exp, now) >= 0) return 0;

        int64_t delta_ns = time_delta_ns(now, exp);
        int64_t interval_ns = interval.count();
        int64_t overruns = delta_ns / interval_ns + 1;

        alarm->expires_ = time_add_ns(exp, overruns * interval_ns);
        return overruns;
    }

    int64_t forward_now(Alarm* alarm, Duration interval) {
        TimePoint now = get_now_for(alarm->type());
        return forward(alarm, now, interval);
    }

    //-----------------------------------------------------------------
    // Query
    //-----------------------------------------------------------------

    Duration expires_remaining(Alarm* alarm) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!alarm->is_armed()) return Duration(INT64_MAX);
        TimePoint now = get_now_for(alarm->type());
        return time_sub(alarm->expires_, now);
    }

    TimePoint next_expiry() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return queue_.first_expiry();
    }

    bool has_pending() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return !queue_.empty();
    }

    size_t pending_count() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return queue_.size();
    }

    //-----------------------------------------------------------------
    // Processing (call from main loop)
    //-----------------------------------------------------------------

    struct AlarmFiredInfo {
        Alarm*    alarm;
        TimePoint now;
        int64_t   latency_ns;
    };

    std::vector<AlarmFiredInfo> process_expired(TimePoint now) {
        std::vector<AlarmFiredInfo> fired;
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        while (!queue_.empty()) {
            Alarm* alarm = queue_.top();
            if (alarm->expires() > now) break;

            queue_.remove(alarm);
            int64_t latency = time_delta_ns(now, alarm->expires());
            alarm->fire(now);

            stats_.record_fire(latency);
            fired.push_back({alarm, now, latency});
        }
        return fired;
    }

    // Suspend/resume handling
    void on_suspend() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        suspended_ = true;
        suspend_time_ = clock_now_monotonic();
    }

    void on_resume() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (suspended_) {
            TimePoint now = clock_now_monotonic();
            Duration sleep_dur = now - suspend_time_;
            total_sleep_dur_ += sleep_dur;
            suspended_ = false;
        }
    }

    Duration total_sleep_duration() const { return total_sleep_dur_; }

    //-----------------------------------------------------------------
    // Statistics
    //-----------------------------------------------------------------

    struct AlarmStats {
        uint64_t armed_count   = 0;
        uint64_t fired_count   = 0;
        uint64_t cancel_count  = 0;
        int64_t  max_latency_ns = 0;

        void record_arm()               { ++armed_count; }
        void record_fire(int64_t lat)   { ++fired_count; if (lat > max_latency_ns) max_latency_ns = lat; }
        void record_cancel()            { ++cancel_count; }
    };

    const AlarmStats& stats() const { return stats_; }
    void reset_stats() { stats_ = AlarmStats{}; }

private:
    TimePoint get_now_for(AlarmType type) const {
        return (type == AlarmType::kRealtime)
            ? clock_now_realtime()
            : clock_now_boottime();
    }

    void remove_locked(Alarm* alarm) {
        queue_.remove(alarm);
    }

    using QueueType = TimerQueue<Alarm>;

    QueueType queue_;
    AlarmStats stats_;
    bool suspended_ = false;
    TimePoint suspend_time_{0};
    Duration total_sleep_dur_{0};
    mutable std::recursive_mutex mutex_;
};

} // namespace engine
