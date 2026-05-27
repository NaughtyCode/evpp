#include "runtime/evpp/event_watcher.h"

#include <cstdio>
#include <string.h>

#include "runtime/core/log/log.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"

namespace evpp {

EventWatcher::EventWatcher(struct event_base* evbase, const Handler& handler)
	: evbase_(evbase), attached_(false), handler_(handler) {
	event_ = new event;
	memset(event_, 0, sizeof(struct event));
}

EventWatcher::EventWatcher(struct event_base* evbase, Handler&& handler)
	: evbase_(evbase), attached_(false), handler_(std::move(handler)) {
	event_ = new event;
	memset(event_, 0, sizeof(struct event));
}

EventWatcher::~EventWatcher() {
	FreeEvent();
	Close();
}

bool EventWatcher::Init() {
	if (!DoInit()) {
		goto failed;
	}

	::event_base_set(evbase_, event_);
	return true;

failed:
	Close();
	return false;
}


void EventWatcher::Close() {
	DoClose();
}

bool EventWatcher::Watch(Duration timeout) {
	struct timeval tv;
	struct timeval* timeoutval = nullptr;
	if (timeout.Nanoseconds() > 0) {
		timeout.To(&tv);
		timeoutval = &tv;
	}

	if (attached_) {
		// When InvokerTimer::periodic_ == true, EventWatcher::Watch will be called many times
		// so we need to remove it from event_base before we add it into event_base.
		// event_del returns -1 when the event is not pending (e.g. after firing), which is expected.
		if (EventDel(event_) != 0) {
			ENGINE_LOG_TRACE(engine::GetLogger(),
							 "event_del fd={} event_={} (may already be non-pending)",
							 this->event_->ev_fd,
							 (void*) event_);
		}
		attached_ = false;
	}

	assert(!attached_);
	if (EventAdd(event_, timeoutval) != 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "event_add failed. fd={} event_={}",
						 this->event_->ev_fd,
						 (void*) event_);
		return false;
	}
	attached_ = true;
	return true;
}

void EventWatcher::FreeEvent() {
	if (event_) {
		if (attached_) {
			EventDel(event_);
			attached_ = false;
		}

		delete (event_);
		event_ = nullptr;
	}
}

void EventWatcher::Cancel() {
	assert(event_);
	FreeEvent();

	if (cancel_callback_) {
		cancel_callback_();
	}
}

void EventWatcher::SetCancelCallback(const Handler& cb) {
	cancel_callback_ = cb;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

PipeEventWatcher::PipeEventWatcher(EventLoop* loop, const Handler& handler)
	: EventWatcher(loop->event_base(), handler) {
	memset(pipe_, 0, sizeof(pipe_[0]) * 2);
}

PipeEventWatcher::PipeEventWatcher(EventLoop* loop, Handler&& h)
	: EventWatcher(loop->event_base(), std::move(h)) {
	memset(pipe_, 0, sizeof(pipe_[0]) * 2);
}


PipeEventWatcher::~PipeEventWatcher() {
	Close();
}

bool PipeEventWatcher::DoInit() {
	assert(pipe_[0] == 0);

#ifdef H_OS_WINDOWS
	// On Windows, wepoll uses AFD (Ancillary Function Driver) polling which
	// may not reliably detect events on AF_UNIX sockets. Use AF_INET loopback
	// instead to ensure the wakeup pipe works correctly with IOCP.
	{
		// RAII socket guard — automatically closes on scope exit unless
		// released.  Eliminates manual EVUTIL_CLOSESOCKET at each goto site.
		struct ScopedSocket {
			evpp_socket_t fd;
			explicit ScopedSocket(evpp_socket_t f = INVALID_SOCKET) : fd(f) {
			}
			~ScopedSocket() {
				if (fd != INVALID_SOCKET) EVUTIL_CLOSESOCKET(fd);
			}
			ScopedSocket(const ScopedSocket&) = delete;
			ScopedSocket& operator=(const ScopedSocket&) = delete;
			evpp_socket_t release() {
				evpp_socket_t f = fd;
				fd = INVALID_SOCKET;
				return f;
			}
		};

		ScopedSocket listener(::socket(AF_INET, SOCK_STREAM, 0));
		if (listener.fd == INVALID_SOCKET) {
			std::fprintf(
				stderr, "[PipeEventWatcher] socket() failed, WSA err=%d\n", ::WSAGetLastError());
			goto failed;
		}

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = 0;

		if (::bind(listener.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
			std::fprintf(
				stderr, "[PipeEventWatcher] bind() failed, WSA err=%d\n", ::WSAGetLastError());
			goto failed;
		}
		if (::listen(listener.fd, 1) < 0) {
			std::fprintf(
				stderr, "[PipeEventWatcher] listen() failed, WSA err=%d\n", ::WSAGetLastError());
			goto failed;
		}

		socklen_t addrlen = sizeof(addr);
		if (::getsockname(listener.fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) < 0) {
			std::fprintf(stderr,
						 "[PipeEventWatcher] getsockname() failed, WSA err=%d\n",
						 ::WSAGetLastError());
			goto failed;
		}

		ScopedSocket writer(::socket(AF_INET, SOCK_STREAM, 0));
		if (writer.fd == INVALID_SOCKET) {
			std::fprintf(stderr,
						 "[PipeEventWatcher] socket(writer) failed, WSA err=%d\n",
						 ::WSAGetLastError());
			goto failed;
		}
		if (::connect(writer.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
			std::fprintf(
				stderr, "[PipeEventWatcher] connect() failed, WSA err=%d\n", ::WSAGetLastError());
			goto failed;
		}

		ScopedSocket reader(::accept(listener.fd, NULL, NULL));
		if (reader.fd == INVALID_SOCKET) {
			std::fprintf(
				stderr, "[PipeEventWatcher] accept() failed, WSA err=%d\n", ::WSAGetLastError());
			goto failed;
		}

		// All sockets created successfully — release from RAII guards.
		// listener is auto-closed by ScopedSocket destructor (needed only for accept).
		pipe_[0] = writer.release();
		pipe_[1] = reader.release();
	}
#else
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pipe_) < 0) {
		int err = EVPP_ERRNO;
		std::fprintf(
			stderr, "[PipeEventWatcher] socketpair() failed, errno=%d %s\n", err, strerror(err));
		ENGINE_LOG_ERROR(
			engine::GetLogger(), "create socketpair ERROR errno={} {}", err, strerror(err));
		goto failed;
	}
#endif

	if (evutil_make_socket_nonblocking(pipe_[0]) < 0 ||
		evutil_make_socket_nonblocking(pipe_[1]) < 0) {
		std::fprintf(stderr, "[PipeEventWatcher] evutil_make_socket_nonblocking() failed\n");
		goto failed;
	}

	::event_set(event_, pipe_[1], EV_READ | EV_PERSIST, &PipeEventWatcher::HandlerFn, this);
	return true;
failed:
	Close();
	return false;
}

void PipeEventWatcher::DoClose() {
	if (pipe_[0] != INVALID_SOCKET) {
		EVUTIL_CLOSESOCKET(pipe_[0]);
		pipe_[0] = INVALID_SOCKET;
	}
	if (pipe_[1] != INVALID_SOCKET) {
		EVUTIL_CLOSESOCKET(pipe_[1]);
		pipe_[1] = INVALID_SOCKET;
	}
}

void PipeEventWatcher::HandlerFn(evpp_socket_t fd, short /*which*/, void* v) {
	ENGINE_LOG_INFO(engine::GetLogger(), "PipeEventWatcher::HandlerFn fd={} v={}", fd, v);
	PipeEventWatcher* e = static_cast<PipeEventWatcher*>(v);
#ifdef H_BENCHMARK_TESTING
	// Every time we only read 1 byte for testing the IO event performance.
	// We use it in the benchmark test program
	//  1. evpp/benchmark/ioevent/evpp/
	//  1. evpp/benchmark/ioevent/fd_channel_vs_pipe_event_watcher/
	char buf[1];
#else
	char buf[128];
#endif
	int n = 0;

	if ((n = ::recv(e->pipe_[1], buf, sizeof(buf), 0)) > 0) {
		e->handler_();
	}
}

bool PipeEventWatcher::AsyncWait() {
	return Watch(Duration());
}

void PipeEventWatcher::Notify() {
	char buf[1] = {};

	if (::send(pipe_[0], buf, sizeof(buf), 0) < 0) {
		// If the send buffer is full (EAGAIN/EWOULDBLOCK), the event loop
		// already has notification data pending and will wake up, so
		// dropping this notification is safe.
		int serrno = EVPP_ERRNO;
		if (serrno != EAGAIN
#ifdef EWOULDBLOCK
			&& serrno != EWOULDBLOCK
#endif
#ifdef WSAEWOULDBLOCK
			&& serrno != WSAEWOULDBLOCK
#endif
		) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "PipeEventWatcher::Notify send failed errno={} {}",
							 serrno,
							 strerror(serrno));
		}
		return;
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
TimerEventWatcher::TimerEventWatcher(EventLoop* loop, const Handler& handler, Duration timeout)
	: EventWatcher(loop->event_base(), handler), timeout_(timeout) {
}

TimerEventWatcher::TimerEventWatcher(EventLoop* loop, Handler&& h, Duration timeout)
	: EventWatcher(loop->event_base(), std::move(h)), timeout_(timeout) {
}

TimerEventWatcher::TimerEventWatcher(struct event_base* loop,
									 const Handler& handler,
									 Duration timeout)
	: EventWatcher(loop, handler), timeout_(timeout) {
}

TimerEventWatcher::TimerEventWatcher(struct event_base* loop, Handler&& h, Duration timeout)
	: EventWatcher(loop, std::move(h)), timeout_(timeout) {
}

bool TimerEventWatcher::DoInit() {
	::event_set(event_, -1, 0, TimerEventWatcher::HandlerFn, this);
	return true;
}

void TimerEventWatcher::HandlerFn(evpp_socket_t /*fd*/, short /*which*/, void* v) {
	TimerEventWatcher* h = static_cast<TimerEventWatcher*>(v);
	h->handler_();
}

bool TimerEventWatcher::AsyncWait() {
	return Watch(timeout_);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
SignalEventWatcher::SignalEventWatcher(signal_number_t signo,
									   EventLoop* loop,
									   const Handler& handler)
	: EventWatcher(loop->event_base(), handler), signo_(signo) {
	assert(signo_);
}

SignalEventWatcher::SignalEventWatcher(signal_number_t signo, EventLoop* loop, Handler&& h)
	: EventWatcher(loop->event_base(), std::move(h)), signo_(signo) {
	assert(signo_);
}

bool SignalEventWatcher::DoInit() {
	assert(signo_);
	signal_set(event_, signo_, SignalEventWatcher::HandlerFn, this);
	return true;
}

void SignalEventWatcher::HandlerFn(signal_number_t /*sn*/, short /*which*/, void* v) {
	SignalEventWatcher* h = static_cast<SignalEventWatcher*>(v);
	h->handler_();
}

bool SignalEventWatcher::AsyncWait() {
	return Watch(Duration());
}
}
