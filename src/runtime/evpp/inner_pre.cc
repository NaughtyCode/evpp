#include "runtime/evpp/inner_pre.h"

#include "runtime/evpp/libevent.h"

#ifndef H_OS_WINDOWS
#include <signal.h>
#endif

#include <map>
#include <thread>
#include <mutex>

namespace evpp {

namespace {
struct OnStartup {
    OnStartup() {
#ifndef H_OS_WINDOWS
        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            std::fprintf(stderr, "[evpp] SIGPIPE set failed, errno=%d\n", EVPP_ERRNO);
        }
#endif
    }
    ~OnStartup() {
    }
} __s_onstartup;
}


#ifdef H_DEBUG_MODE
static std::map<struct event*, std::thread::id> evmap;
static std::mutex mutex;
#endif

int EventAdd(struct event* ev, const struct timeval* timeout) {
#ifdef H_DEBUG_MODE
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (evmap.find(ev) == evmap.end()) {
            auto id = std::this_thread::get_id();
            evmap[ev] = id;
        } else {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Event {} fd={} event_add twice!", (void*)ev, ev->ev_fd);
            assert(false && "event_add twice");
        }
    }
    ENGINE_LOG_DEBUG(engine::GetLogger(), "event_add ev={} fd={} user_ptr={} tid={}", (void*)ev, ev->ev_fd, ev->ev_arg, std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    return event_add(ev, timeout);
}

int EventDel(struct event* ev) {
#ifdef H_DEBUG_MODE
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = evmap.find(ev);
        if (it == evmap.end()) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Event {} fd={} not exist in event loop, maybe event_del twice.", (void*)ev, ev->ev_fd);
            assert(false && "event_del twice");
        } else {
            auto id = std::this_thread::get_id();
            if (id != it->second) {
                ENGINE_LOG_ERROR(engine::GetLogger(), "Event {} fd={} deleted in different thread.", (void*)ev, ev->ev_fd);
                assert(it->second == id);
            }
            evmap.erase(it);
        }
    }
    ENGINE_LOG_DEBUG(engine::GetLogger(), "event_del ev={} fd={} user_ptr={} tid={}", (void*)ev, ev->ev_fd, ev->ev_arg, std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    return event_del(ev);
}

int GetActiveEventCount() {
#ifdef H_DEBUG_MODE
    std::lock_guard<std::mutex> guard(mutex);
    return static_cast<int>(evmap.size());
#else
    return 0;
#endif
}

}
