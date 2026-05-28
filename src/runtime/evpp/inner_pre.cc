#include "runtime/evpp/inner_pre.h"

#include "runtime/evpp/libevent.h"

#ifndef H_OS_WINDOWS
#include <signal.h>
#endif

#include <map>
#include <mutex>
#include <thread>

namespace evpp {

namespace {
struct OnStartup {
	OnStartup() {
#ifndef H_OS_WINDOWS
		if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
			ENGINE_LOG_ERROR(engine::GetLogger(), "[evpp] SIGPIPE set failed, errno={}", EVPP_ERRNO);
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

// Thread-local tracking of the active event_base.
// Set by EventLoop::Run() before event_base_dispatch, cleared after.
// Used by lightweight cross-thread check in Release builds.
static thread_local struct event_base* tls_event_base = nullptr;

void SetTlsEventBase(struct event_base* base) {
	tls_event_base = base;
}

void ClearTlsEventBase() {
	tls_event_base = nullptr;
}

struct event_base* GetTlsEventBase() {
	return tls_event_base;
}

int EventAdd(struct event* ev, const struct timeval* timeout) {
#ifdef H_DEBUG_MODE
	{
		std::lock_guard<std::mutex> guard(mutex);
		if (evmap.find(ev) == evmap.end()) {
			auto id = std::this_thread::get_id();
			evmap[ev] = id;
		} else {
			ENGINE_LOG_ERROR(
				engine::GetLogger(), "Event {} fd={} event_add twice!", (void*) ev, ev->ev_fd);
			assert(false && "event_add twice");
		}
	}
	ENGINE_LOG_DEBUG(engine::GetLogger(),
					 "event_add ev={} fd={} user_ptr={} tid={}",
					 (void*) ev,
					 ev->ev_fd,
					 ev->ev_arg,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()));
#else
	// Lightweight cross-thread check for Release builds.
	if (tls_event_base) {
		struct event_base* ev_base = event_get_base(ev);
		if (ev_base && ev_base != tls_event_base) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "event_add from wrong thread! ev={} fd={} ev_base={} tls_base={}",
							 (void*)ev, ev->ev_fd,
							 (void*)ev_base, (void*)tls_event_base);
		}
	}
#endif
	return event_add(ev, timeout);
}

int EventDel(struct event* ev) {
#ifdef H_DEBUG_MODE
	{
		std::lock_guard<std::mutex> guard(mutex);
		auto it = evmap.find(ev);
		if (it == evmap.end()) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "Event {} fd={} not exist in event loop, maybe event_del twice.",
							 (void*) ev,
							 ev->ev_fd);
			assert(false && "event_del twice");
		} else {
			auto id = std::this_thread::get_id();
			if (id != it->second) {
				ENGINE_LOG_ERROR(engine::GetLogger(),
								 "Event {} fd={} deleted in different thread.",
								 (void*) ev,
								 ev->ev_fd);
				assert(it->second == id);
			}
			evmap.erase(it);
		}
	}
	ENGINE_LOG_DEBUG(engine::GetLogger(),
					 "event_del ev={} fd={} user_ptr={} tid={}",
					 (void*) ev,
					 ev->ev_fd,
					 ev->ev_arg,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()));
#else
	// Lightweight cross-thread check for Release builds.
	if (tls_event_base) {
		struct event_base* ev_base = event_get_base(ev);
		if (ev_base && ev_base != tls_event_base) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "event_del from wrong thread! ev={} fd={} ev_base={} tls_base={}",
							 (void*)ev, ev->ev_fd,
							 (void*)ev_base, (void*)tls_event_base);
		}
	}
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
