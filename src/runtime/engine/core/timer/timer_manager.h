// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// TimerManager — the main public API for the GameTimer library.
// Integrates high-resolution timers, timer wheel, alarm timers,
// clock sources, and timekeeping into a single, easy-to-use interface.
//
// Usage:
//   #include "engine/core/timer/timer.h"
//
//   engine::TimerManager tm;
//   tm.initialize();
//
//   // Create a one-shot timer
//   auto* timer = tm.create_timer([](auto* t) {
//       printf("Fired!\n");
//       return engine::TimerResult::kNoRestart;
//   });
//   tm.start_timer(timer, engine::ms_to_time(500));
//
//   // Main loop
//   while (running) {
//       tm.update();  // fires expired timers
//   }

#pragma once

#include "engine/core/timer/timer_core.h"
#include "engine/core/timer/hr_timer.h"
#include "engine/core/timer/timer_wheel.h"
#include "engine/core/timer/alarm_timer.h"
#include "engine/core/timer/clock_source.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

//=============================================================================
// TimerHandle — unique identifier for a timer
//=============================================================================

using TimerId = uint64_t;
inline constexpr TimerId kInvalidTimerId = 0;

//=============================================================================
// TimerManager — central timer management system
//=============================================================================

class TimerManager {
public:
    //=================================================================
    // Construction
    //=================================================================

    TimerManager();
    ~TimerManager();

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;
    TimerManager(TimerManager&&) = delete;
    TimerManager& operator=(TimerManager&&) = delete;

    //=================================================================
    // Initialization / shutdown
    //=================================================================

    void initialize();
    void shutdown();

    bool is_initialized() const { return initialized_.load(); }

    //=================================================================
    // Main loop update — call every frame / tick
    // Processes all expired timers across all subsystems.
    //=================================================================

    struct UpdateResult {
        size_t   hrtimers_fired     = 0;
        size_t   wheel_timers_fired = 0;
        size_t   alarms_fired       = 0;
        size_t   total_fired        = 0;
        Duration elapsed;              // time since last update
        TimePoint current_time;       // current monotonic time
        int64_t  next_event_ns = 0;   // time until next event (>0)
    };

    UpdateResult update();
    UpdateResult update(TimePoint now);  // override current time (testing)

    //=================================================================
    // Time query API (mirrors ktime_get* family)
    //=================================================================

    TimePoint now() const;               // monotonic time (ktime_get)
    TimePoint now_real() const;          // real/wall time (ktime_get_real)
    TimePoint now_boottime() const;      // boot time (ktime_get_boottime)
    TimePoint now_tai() const;           // TAI time (ktime_get_clocktai)
    TimePoint now_raw() const;           // raw monotonic (ktime_get_raw)

    int64_t now_ns() const;              // ktime_get_ns
    int64_t now_real_ns() const;         // ktime_get_real_ns
    int64_t now_boottime_ns() const;     // ktime_get_boottime_ns

    // Coarse-grained time (faster, lower precision)
    TimePoint now_coarse() const;        // ktime_get_coarse
    int64_t now_coarse_ns() const;       // ktime_get_coarse_ns

    // Fast-path time (lock-free, for perf-critical code)
    int64_t now_fast_ns() const;         // ktime_get_mono_fast_ns

    int64_t resolution_ns() const;       // clock resolution

    //=================================================================
    // High-Resolution Timer API
    //=================================================================

    // Create a timer with a callback.
    // Returns a TimerId for future reference.
    TimerId create_timer(HrTimerNode::Callback callback,
                         ClockId clock_id = ClockId::kMonotonic,
                         TimerMode mode = TimerMode::kDefault);

    // Create a simple void-callback timer (fire and forget)
    TimerId create_simple_timer(std::function<void()> callback,
                                ClockId clock_id = ClockId::kMonotonic);

    // Start / arm a timer
    void start_timer(TimerId id, TimePoint expiry, TimerMode mode);
    void start_timer_relative(TimerId id, Duration relative_time, TimerMode mode = TimerMode::kRelative);
    void start_timer_range(TimerId id, TimePoint expiry, int64_t range_ns, TimerMode mode);

    // Cancel a timer
    bool cancel_timer(TimerId id);

    // Restart a timer using its existing expiry values
    void restart_timer(TimerId id);

    // Forward a repeating timer by one interval
    int64_t forward_timer(TimerId id, Duration interval);

    // Query timer state
    bool      is_timer_active(TimerId id) const;
    Duration  timer_remaining(TimerId id) const;
    TimePoint timer_expires(TimerId id) const;
    TimerState timer_state(TimerId id) const;

    // Destroy a timer (cancel first if active)
    void destroy_timer(TimerId id);

    // Update a timer's callback
    void set_timer_callback(TimerId id, HrTimerNode::Callback callback);

    //=================================================================
    // Timer Wheel API (low-resolution, bulk timers)
    //=================================================================

    // Create a wheel timer with millisecond-precision
    TimerId create_wheel_timer(TimerWheelNode::Callback callback,
                               uint32_t flags = 0);

    // Start a wheel timer (expires_ms: milliseconds from now)
    void start_wheel_timer(TimerId id, int64_t expires_ms);
    void mod_wheel_timer(TimerId id, int64_t expires_ms);
    bool cancel_wheel_timer(TimerId id);
    bool wheel_timer_pending(TimerId id) const;

    //=================================================================
    // Alarm Timer API
    //=================================================================

    TimerId create_alarm(AlarmType type, Alarm::Callback callback);
    void start_alarm(TimerId id, TimePoint start_time);
    void start_alarm_relative(TimerId id, Duration relative_time);
    bool cancel_alarm(TimerId id);
    Duration alarm_remaining(TimerId id) const;
    int64_t forward_alarm(TimerId id, Duration interval);
    void restart_alarm(TimerId id);

    //=================================================================
    // Convenience: std::chrono integration
    //=================================================================

    // Create a timer that fires after a chrono duration
    template <typename Rep, typename Period>
    TimerId create_timer_for(std::chrono::duration<Rep, Period> dur,
                             std::function<void()> callback) {
        TimerId id = create_simple_timer(std::move(callback));
        start_timer_relative(id, std::chrono::duration_cast<Duration>(dur));
        return id;
    }

    // Create a repeating timer at a fixed interval
    TimerId create_repeating_timer(Duration interval,
                                   HrTimerNode::Callback callback,
                                   ClockId clock_id = ClockId::kMonotonic);

    // Create a repeating simple callback timer
    TimerId create_repeating_simple_timer(Duration interval,
                                          std::function<void()> callback,
                                          ClockId clock_id = ClockId::kMonotonic);

    //=================================================================
    // Suspend / Resume (game pause handling)
    //=================================================================

    void on_suspend();
    void on_resume();
    bool is_suspended() const;

    Duration total_suspend_duration() const;

    //=================================================================
    // Time adjustment (testing, cheats, corrections)
    //=================================================================

    void inject_sleep_time(Duration delta);
    void set_time_scale(double scale);   // 1.0 = normal, 0.5 = half speed, 2.0 = double speed

    double time_scale() const;

    //=================================================================
    // Clock source management
    //=================================================================

    ClockManager& clock_manager();
    const ClockManager& clock_manager() const;

    void set_clock_source(std::unique_ptr<ClockSource> cs);

    //=================================================================
    // Statistics and introspection
    //=================================================================

    struct ManagerStats {
        TimerStats  hrtimer_stats;
        TimerStats  wheel_stats;
        size_t      total_hrtimers        = 0;
        size_t      total_wheel_timers    = 0;
        size_t      total_alarms          = 0;
        size_t      active_hrtimers       = 0;
        size_t      active_wheel_timers   = 0;
        size_t      active_alarms         = 0;
        uint64_t    total_updates         = 0;
        int64_t     avg_update_time_us    = 0;
        double      time_scale            = 1.0;
        bool        suspended             = false;
        Duration    total_suspend_dur;
    };

    ManagerStats stats() const;
    void reset_stats();

    // List all active timer IDs (for debugging)
    std::vector<TimerId> active_timers() const;

    // Dump all timer state to console (debug)
    void dump_state() const;

    //=================================================================
    // Global singleton access (optional convenience)
    //=================================================================

    static TimerManager& instance();
    static TimerManager& create_instance();
    static void destroy_instance();

private:
    // Internal timer entry
    struct TimerEntry {
        enum class Kind { kHrTimer, kWheelTimer, kAlarm };

        Kind kind = Kind::kHrTimer;
        union {
            HrTimerNode*    hrtimer;
            TimerWheelNode* wheel_timer;
            Alarm*          alarm;
        };

        TimerEntry() : hrtimer(nullptr) {}
        ~TimerEntry() {
            switch (kind) {
                case Kind::kHrTimer:   delete hrtimer; break;
                case Kind::kWheelTimer: delete wheel_timer; break;
                case Kind::kAlarm:      delete alarm; break;
            }
        }

        TimerEntry(const TimerEntry&) = delete;
        TimerEntry& operator=(const TimerEntry&) = delete;
        TimerEntry(TimerEntry&& other) noexcept : kind(other.kind), hrtimer(other.hrtimer) {
            other.hrtimer = nullptr;
            other.kind = Kind::kHrTimer;
        }
    };

    TimerId allocate_id();
    void free_id(TimerId id);

    TimerEntry* get_entry(TimerId id);
    const TimerEntry* get_entry(TimerId id) const;

    // Internal update helpers
    UpdateResult update_hrtimers(UpdateResult result, TimePoint now);
    UpdateResult update_wheel(UpdateResult result);
    UpdateResult update_alarms(UpdateResult result, TimePoint now);

    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> suspended_{false};
    std::atomic<double> time_scale_{1.0};

    // Timer subsystems
    std::unique_ptr<HrTimerManager>   hrtimer_mgr_;
    std::unique_ptr<TimerWheel>       wheel_;
    std::unique_ptr<AlarmTimerManager> alarm_mgr_;

    // Timer ID management
    mutable std::mutex entries_mutex_;
    std::unordered_map<TimerId, std::unique_ptr<TimerEntry>> entries_;
    std::atomic<TimerId> next_id_{1};

    // Time tracking
    TimePoint last_update_time_{0};
    Duration suspend_offset_{0};
    TimePoint suspend_start_{0};

    // Clock management
    ClockManager clock_mgr_;

    // Global singleton
    static std::unique_ptr<TimerManager> instance_;
    static std::mutex instance_mutex_;

    // Statistics
    mutable ManagerStats stats_;
    uint64_t total_update_time_ns_ = 0;
};

//=============================================================================
// Inline convenience functions
//=============================================================================

inline TimerId create_timeout(Duration timeout, std::function<void()> callback) {
    return TimerManager::instance().create_timer_for(timeout, std::move(callback));
}

inline TimerId create_interval(Duration interval, std::function<void()> callback) {
    return TimerManager::instance().create_repeating_simple_timer(
        interval, std::move(callback));
}

inline void cancel(TimerId id) {
    TimerManager::instance().cancel_timer(id);
}

} // namespace engine
