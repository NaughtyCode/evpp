#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/event_loop_thread_pool.h"
#include "runtime/evpp/event_loop.h"

namespace evpp {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop, uint32_t thread_number)
    : base_loop_(base_loop),
      thread_num_(thread_number) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread_num={} base loop={}", (void*)this, thread_num(), (void*)base_loop_);
}

EventLoopThreadPool::~EventLoopThreadPool() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread_num={}", (void*)this, thread_num());
    Join();
    threads_.clear();
}

bool EventLoopThreadPool::Start(bool wait_thread_started) {
    status_.store(kStarting);
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread_num={} base loop={} wait_thread_started={}", (void*)this, thread_num(), (void*)base_loop_, wait_thread_started);

    if (thread_num_ == 0) {
        status_.store(kRunning);
        return true;
    }

    std::shared_ptr<std::atomic<uint32_t>> started_count(new std::atomic<uint32_t>(0));
    std::shared_ptr<std::atomic<uint32_t>> exited_count(new std::atomic<uint32_t>(0));
    for (uint32_t i = 0; i < thread_num_; ++i) {
        auto prefn = [this, started_count]() {
            ENGINE_LOG_TRACE(engine::GetLogger(), "a working thread started tid={}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
            this->OnThreadStarted(started_count->fetch_add(1) + 1);
            return EventLoopThread::kOK;
        };

        auto postfn = [this, exited_count]() {
            ENGINE_LOG_TRACE(engine::GetLogger(), "a working thread exiting, tid={}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
            this->OnThreadExited(exited_count->fetch_add(1) + 1);
            return EventLoopThread::kOK;
        };

        EventLoopThreadPtr t(new EventLoopThread());
        if (!t->Start(wait_thread_started, prefn, postfn)) {
            //FIXME error process
            ENGINE_LOG_ERROR(engine::GetLogger(), "start thread failed!");
            return false;
        }

        std::stringstream ss;
        ss << "EventLoopThreadPool-thread-" << i << "th";
        t->set_name(ss.str());
        threads_.push_back(t);
    }

    // when all the working thread have started,
    // status_ will be stored with kRunning in method OnThreadStarted

    if (wait_thread_started) {
        while (!IsRunning()) {
            usleep(1);
        }
        assert(status_.load() == kRunning);
    }

    return true;
}

void EventLoopThreadPool::Stop(bool wait_thread_exit) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} wait_thread_exit={}", (void*)this, wait_thread_exit);
    Stop(wait_thread_exit, DoneCallback());
}

void EventLoopThreadPool::Stop(DoneCallback fn) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*)this);
    Stop(false, fn);
}

void EventLoopThreadPool::Stop(bool wait_thread_exit, DoneCallback fn) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*)this);
    status_.store(kStopping);

    if (thread_num_ == 0) {
        status_.store(kStopped);

        if (fn) {
            ENGINE_LOG_TRACE(engine::GetLogger(), "this={} calling stopped callback", (void*)this);
            fn();
        }
        return;
    }

    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} wait_thread_exit={}", (void*)this, wait_thread_exit);
    stopped_cb_ = fn;

    for (auto &t : threads_) {
        t->Stop();
    }

    // when all the working thread have stopped
    // status_ will be stored with kStopped in method OnThreadExited

    auto is_stopped_fn = [this]() {
        for (auto &t : this->threads_) {
            if (!t->IsStopped()) {
                return false;
            }
        }
        return true;
    };

    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} before promise wait", (void*)this);
    if (thread_num_ > 0 && wait_thread_exit) {
        while (!is_stopped_fn()) {
            usleep(1);
        }
    }
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} after promise wait", (void*)this);

    status_.store(kStopped);
}

void EventLoopThreadPool::Join() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread_num={}", (void*)this, thread_num());
    for (auto &t : threads_) {
        t->Join();
    }
    threads_.clear();
}

void EventLoopThreadPool::AfterFork() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread_num={}", (void*)this, thread_num());
    for (auto &t : threads_) {
        t->AfterFork();
    }
}

EventLoop* EventLoopThreadPool::GetNextLoop() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*)this);
    EventLoop* loop = base_loop_;

    if (IsRunning() && !threads_.empty()) {
        // No need to lock here
        int64_t next = next_.fetch_add(1);
        next = next % threads_.size();
        loop = (threads_[next])->loop();
    }

    return loop;
}

EventLoop* EventLoopThreadPool::GetNextLoopWithHash(uint64_t hash) {
    EventLoop* loop = base_loop_;

    if (IsRunning() && !threads_.empty()) {
        uint64_t next = hash % threads_.size();
        loop = (threads_[next])->loop();
    }

    return loop;
}

uint32_t EventLoopThreadPool::thread_num() const {
    return thread_num_;
}

void EventLoopThreadPool::OnThreadStarted(uint32_t count) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} tid={} count={} started.", (void*)this, std::hash<std::thread::id>{}(std::this_thread::get_id()), count);
    if (count == thread_num_) {
        ENGINE_LOG_TRACE(engine::GetLogger(), "this={} thread pool totally started.", (void*)this);
        status_.store(kRunning);
    }
}

void EventLoopThreadPool::OnThreadExited(uint32_t count) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} tid={} count={} exited.", (void*)this, std::hash<std::thread::id>{}(std::this_thread::get_id()), count);
    if (count == thread_num_) {
        status_.store(kStopped);
        ENGINE_LOG_TRACE(engine::GetLogger(), "this={} this is the last thread stopped. Thread pool totally exited.", (void*)this);
        if (stopped_cb_) {
            stopped_cb_();
            stopped_cb_ = DoneCallback();
        }
    }
}

}
