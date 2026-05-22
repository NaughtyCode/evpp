// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// Cascading timer wheel — efficient bulk timer management for timeouts.
// Mirrors the Linux kernel timer wheel (kernel/time/timer.c).
//
// The timer wheel provides O(1) insert and amortized O(1) expiry for timers
// with coarse granularity. It uses a multi-level hash wheel where each level
// has progressively larger granularity.
//
// Level  Granularity     Range (at HZ=1000)
//   0       1 ms            0 - 63 ms
//   1       8 ms           64 ms - 511 ms
//   2      64 ms          512 ms - ~4 s
//   3     512 ms           ~4 s - ~32 s
//   4    4096 ms (~4s)    ~32 s - ~4 min
//   5   32768 ms (~32s)   ~4 min - ~34 min
//   6  262144 ms (~4min)  ~34 min - ~4 h
//   7 2097152 ms (~34min) ~4 h - ~1 day
//   8 16777216 ms (~4h)   ~1 day - ~12 days

#pragma once

#include "engine/core/timer/timer_core.h"

#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

namespace engine {

//=============================================================================
// TimerWheelNode — a node in the timer wheel
//=============================================================================

class TimerWheelNode {
public:
    using Callback = std::function<void(TimerWheelNode* timer)>;

    TimerWheelNode() = default;

    void setup(Callback callback, uint32_t flags = 0) {
        callback_ = std::move(callback);
        flags_ = flags;
        state_ = TimerState::kInactive;
    }

    ~TimerWheelNode() = default;

    TimerWheelNode(const TimerWheelNode&) = delete;
    TimerWheelNode& operator=(const TimerWheelNode&) = delete;
    TimerWheelNode(TimerWheelNode&& other) noexcept
        : callback_(std::move(other.callback_))
        , flags_(other.flags_)
        , user_data_(other.user_data_)
        , bucket_index_(other.bucket_index_)
        , bucket_it_(other.bucket_it_) {
        expires_.store(other.expires_.load());
        state_.store(other.state_.load());
        other.bucket_index_ = -1;
        other.clear_bucket_iterator();
    }
    TimerWheelNode& operator=(TimerWheelNode&& other) noexcept {
        // TimerNode must not be queued when moved — use del_timer first.
        // If this assert fires, the caller moved a timer while it was
        // still in a wheel bucket, which leaves a dangling pointer.
        assert(bucket_index_ < 0);
        if (this != &other) {
            callback_ = std::move(other.callback_);
            flags_ = other.flags_;
            user_data_ = other.user_data_;
            bucket_index_ = other.bucket_index_;
            bucket_it_ = other.bucket_it_;
            expires_.store(other.expires_.load());
            state_.store(other.state_.load());
            other.bucket_index_ = -1;
            other.clear_bucket_iterator();
        }
        return *this;
    }

    TimePoint  expires()   const { return expires_.load(); }
    uint32_t   flags()     const { return flags_; }
    TimerState state()     const { return state_.load(); }
    void*      user_data() const { return user_data_; }

    bool is_queued()     const { return state_.load() == TimerState::kArmed; }
    bool is_deferrable() const { return (flags_ & kFlagDeferrable) != 0; }
    bool is_pinned()     const { return (flags_ & kFlagPinned) != 0; }
    bool is_irq_safe()   const { return (flags_ & kFlagIrqSafe) != 0; }

    void set_user_data(void* data) { user_data_ = data; }
    void set_expires(TimePoint t)  { expires_.store(t); }
    void set_flags(uint32_t f)     { flags_ = f; }
    void add_flag(uint32_t f)      { flags_ |= f; }
    void clear_flag(uint32_t f)    { flags_ &= ~f; }

    // Flag constants (mirror TIMER_DEFERRABLE, TIMER_PINNED, etc.)
    static constexpr uint32_t kFlagDeferrable = 0x00080000;
    static constexpr uint32_t kFlagPinned     = 0x00100000;
    static constexpr uint32_t kFlagIrqSafe    = 0x00200000;

    void fire() {
        state_ = TimerState::kFiring;
        if (callback_) {
            callback_(this);
        }
        state_ = TimerState::kInactive;
    }

    // For bucket list management
    using BucketIterator = typename std::list<TimerWheelNode*>::iterator;
    void set_bucket_iterator(BucketIterator it) { bucket_it_ = it; }
    BucketIterator bucket_iterator() const { return bucket_it_; }
    void clear_bucket_iterator() { bucket_it_ = BucketIterator(); }

private:
    friend class TimerWheel;

    std::atomic<TimePoint>  expires_{TimePoint(0)};
    std::atomic<TimerState> state_{TimerState::kInactive};
    Callback   callback_;
    uint32_t   flags_ = 0;
    void*      user_data_ = nullptr;
    BucketIterator bucket_it_;
    int bucket_index_ = -1;  // for O(1) removal; -1 = not in any bucket
};

//=============================================================================
// TimerWheel — cascading multi-level hash wheel
//=============================================================================

class TimerWheel {
public:
    static constexpr int kLvlBits  = 6;
    static constexpr int kLvlSize  = 1 << kLvlBits;
    static constexpr int kLvlMask  = kLvlSize - 1;
    static constexpr int kLvlDepth = 9;
    static constexpr int kLvlClkShift = 3;

    static constexpr int kWheelSize = kLvlSize * kLvlDepth;

    static constexpr int64_t kLevelGran(int n) { return 1LL << (n * kLvlClkShift); }
    static constexpr int64_t kLevelStart(int n) {
        return (kLvlSize - 1) << ((n - 1) * kLvlClkShift);
    }

    static constexpr int64_t kMaxTimeoutMs() {
        return kLevelStart(kLvlDepth) - kLevelGran(kLvlDepth - 1);
    }

    TimerWheel() {
        for (int i = 0; i < kWheelSize; ++i) {
            pending_[i].store(0);
        }
    }

    ~TimerWheel() = default;
    TimerWheel(const TimerWheel&) = delete;
    TimerWheel& operator=(const TimerWheel&) = delete;

    //-----------------------------------------------------------------
    // Timer management
    //-----------------------------------------------------------------

    void add_timer(TimerWheelNode* timer, int64_t expires_ms) {
        assert(timer);
        assert(expires_ms >= 0);
        std::lock_guard<std::mutex> lock(mutex_);

        if (timer->is_queued()) {
            remove_timer_locked(timer);
        }

        int64_t cutoff = kMaxTimeoutMs();
        if (expires_ms > cutoff) expires_ms = cutoff;

        int64_t base = jiffies_.load();
        int idx = calc_index(expires_ms, base);
        timer->set_expires(Duration((base + expires_ms) * kNsPerMs));
        timer->state_ = TimerState::kArmed;
        enqueue_locked(timer, idx);
        stats_.record_arm();
    }

    bool mod_timer(TimerWheelNode* timer, int64_t expires_ms) {
        assert(timer);
        std::lock_guard<std::mutex> lock(mutex_);

        if (timer->is_queued()) remove_timer_locked(timer);
        if (expires_ms > kMaxTimeoutMs()) expires_ms = kMaxTimeoutMs();
        if (expires_ms < 0) expires_ms = 0;

        int64_t base = jiffies_.load();
        int idx = calc_index(expires_ms, base);
        timer->set_expires(Duration((base + expires_ms) * kNsPerMs));
        timer->state_ = TimerState::kArmed;
        enqueue_locked(timer, idx);
        return true;
    }

    bool del_timer(TimerWheelNode* timer) {
        assert(timer);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!timer->is_queued()) return false;
        remove_timer_locked(timer);
        timer->state_ = TimerState::kCancelled;
        stats_.record_cancel();
        return true;
    }

    bool timer_pending(TimerWheelNode* timer) const {
        return timer->is_queued();
    }

    //-----------------------------------------------------------------
    // Timer expiry
    //-----------------------------------------------------------------

    std::vector<TimerWheelNode*> advance_one_jiffy() {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = jiffies_ % kLvlSize;
        auto expired = collect_expired_locked(idx);
        for (size_t i = 0; i < expired.size(); ++i) {
            stats_.record_expire(0);
        }
        ++jiffies_;
        cascade_all_locked();
        return expired;
    }

    std::vector<TimerWheelNode*> advance(int64_t num_jiffies) {
        std::vector<TimerWheelNode*> all_expired;
        std::lock_guard<std::mutex> lock(mutex_);
        for (int64_t i = 0; i < num_jiffies; ++i) {
            int idx = jiffies_ % kLvlSize;
            auto batch = collect_expired_locked(idx);
            for (size_t j = 0; j < batch.size(); ++j) {
                stats_.record_expire(0);
            }
            all_expired.insert(all_expired.end(), batch.begin(), batch.end());
            ++jiffies_;
            cascade_all_locked();
        }
        return all_expired;
    }

    //-----------------------------------------------------------------
    // Query
    //-----------------------------------------------------------------

    int64_t current_jiffy() const { return jiffies_.load(); }
    size_t  active_count() const {
        size_t count = 0;
        for (int i = 0; i < kWheelSize; ++i) count += pending_[i].load();
        return count;
    }

    int64_t next_expiry_ms() const {
        int64_t start = jiffies_.load();

        // Level 0 — exact jiffy match. Scan the next 64 jiffies.
        for (int64_t offset = 0; offset < kLvlSize; ++offset) {
            int idx = (start + offset) & kLvlMask;
            if (pending_[idx].load() > 0) return start + offset;
        }

        // Higher levels — find the earliest cascade point that has timers.
        // Each cascade moves timers from a higher-level bucket into level 0,
        // so the cascade jiffy is a lower bound on when those timers can fire.
        int64_t best = INT64_MAX;
        for (int level = 1; level < kLvlDepth; ++level) {
            int64_t gran = kLevelGran(level);
            int64_t shift = kLvlClkShift * level;
            int64_t cur_pos = (start >> shift) & kLvlMask;
            int base_idx = kLvlSize * level;

            for (int bucket = 0; bucket < kLvlSize; ++bucket) {
                if (pending_[base_idx + bucket].load() == 0) continue;
                // How many level-rotations until the cascade hits this bucket?
                int64_t steps = (bucket - cur_pos) & kLvlMask;
                if (steps == 0) steps = kLvlSize; // need one full rotation
                int64_t cascade_jf = start + steps * gran;
                if (cascade_jf < best) best = cascade_jf;
            }
        }
        return best;
    }

    const TimerStats& stats() const { return stats_; }
    void reset_stats() { stats_ = TimerStats{}; }

    size_t bucket_count(int idx) const {
        if (idx < 0 || idx >= kWheelSize) return 0;
        return pending_[idx].load();
    }

    static constexpr int level_count() { return kLvlDepth; }
    static constexpr int buckets_per_level() { return kLvlSize; }
    static constexpr int64_t level_granularity_ms(int level) {
        return kLevelGran(level);
    }
    static int level_for_ms(int64_t ms) { return calc_level(ms); }

private:
    static int calc_index(int64_t expires_ms, int64_t base_jiffies) {
        int64_t abs_expires = base_jiffies + expires_ms;
        int level = calc_level(expires_ms);
        int off = kLvlSize * level;
        int bucket = (abs_expires >> (kLvlClkShift * level)) & kLvlMask;
        return off + bucket;
    }

    static int calc_level(int64_t delta) {
        if (delta < kLvlSize) return 0;
        for (int level = 1; level < kLvlDepth; ++level) {
            if (delta < (static_cast<int64_t>(kLvlSize) << (kLvlClkShift * level))) {
                return level;
            }
        }
        return kLvlDepth - 1;
    }

    void enqueue_locked(TimerWheelNode* timer, int idx) {
        buckets_[idx].push_back(timer);
        timer->set_bucket_iterator(--buckets_[idx].end());
        timer->bucket_index_ = idx;
        pending_[idx].fetch_add(1);
    }

    void remove_timer_locked(TimerWheelNode* timer) {
        int idx = timer->bucket_index_;
        if (idx < 0 || idx >= kWheelSize) return;
        auto it = timer->bucket_iterator();
        buckets_[idx].erase(it);
        pending_[idx].fetch_sub(1);
        timer->clear_bucket_iterator();
        timer->bucket_index_ = -1;
    }

    std::vector<TimerWheelNode*> collect_expired_locked(int idx) {
        std::vector<TimerWheelNode*> expired;
        auto& bucket = buckets_[idx];
        expired.assign(bucket.begin(), bucket.end());
        for (auto* n : expired) {
            n->clear_bucket_iterator();
            n->bucket_index_ = -1;
        }
        bucket.clear();
        pending_[idx].store(0);
        return expired;
    }

    // Cascade timers from higher level into lower levels when the
    // jiffies counter reaches a cascade boundary. This is the key
    // mechanism that makes the multi-level wheel work: timers
    // trickle down from coarse-grained buckets to fine-grained
    // buckets as their expiry time approaches.
    void cascade_locked(int source_level) {
        int src_idx = kLvlSize * source_level
                    + ((jiffies_.load() >> (kLvlClkShift * source_level)) & kLvlMask);
        auto& src_bucket = buckets_[src_idx];
        if (src_bucket.empty()) return;

        // Take timers out of the source bucket
        std::vector<TimerWheelNode*> timers(src_bucket.begin(), src_bucket.end());
        src_bucket.clear();
        pending_[src_idx].store(0);

        int64_t now_jiffies = jiffies_.load();
        for (auto* timer : timers) {
            timer->clear_bucket_iterator();
            timer->bucket_index_ = -1;
            // Re-index based on remaining time from now
            int64_t remaining = (timer->expires().count() / kNsPerMs) - now_jiffies;
            if (remaining < 0) remaining = 0;
            int new_idx = calc_index(remaining, now_jiffies);
            enqueue_locked(timer, new_idx);
        }
    }

    void cascade_all_locked() {
        // Cascade from each level into the level below it at the
        // granularity boundary of that level. Level 1 (gran 8ms)
        // cascades every 8 jiffies, level 2 (gran 64ms) every 64,
        // etc. This ensures every bucket at every level is cascaded.
        for (int src_level = 1; src_level < kLvlDepth; ++src_level) {
            int64_t mask = kLevelGran(src_level) - 1;
            if ((jiffies_.load() & mask) == 0) {
                cascade_locked(src_level);
            }
        }
    }

    std::array<std::list<TimerWheelNode*>, kWheelSize> buckets_;
    std::array<std::atomic<size_t>, kWheelSize> pending_;
    std::atomic<int64_t> jiffies_{0};
    TimerStats stats_;
    mutable std::mutex mutex_;
};

} // namespace engine
