#include "runtime/evpp/event_loop.h"

#include <cstdio>

#include "runtime/evpp/event_watcher.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/invoke_timer.h"
#include "runtime/evpp/libevent.h"

namespace evpp {

// Safe trace macro — skips logging if the engine logger hasn't been
// initialised yet.  EventLoop may be constructed before InitLogger(),
// so every EVPP_TRACE( ...) call must be
// null-guarded.
#define EVPP_TRACE(fmt, ...)                                \
	do {                                                    \
		auto* _l_ = engine::GetLogger();                    \
		if (_l_) ENGINE_LOG_TRACE(_l_, fmt, ##__VA_ARGS__); \
	} while (0)

EventLoop::EventLoop()
	: evbase_(nullptr), create_evbase_myself_(true), notified_(false), pending_functor_count_(0) {
	EVPP_TRACE("this={}", (void*) this);
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	struct event_config* cfg = event_config_new();
	if (cfg) {
		// Does not cache time to get a preciser timer
		event_config_set_flag(cfg, EVENT_BASE_FLAG_NO_CACHE_TIME);
		evbase_ = event_base_new_with_config(cfg);
		event_config_free(cfg);
	}
#else
	evbase_ = event_base_new();
#endif
	if (!evbase_) {
		std::fprintf(stderr, "[EventLoop] FATAL: failed to create event_base\n");
		std::exit(EXIT_FAILURE);
	}
	Init();
}

EventLoop::EventLoop(struct event_base* base)
	: evbase_(base), create_evbase_myself_(false), notified_(false), pending_functor_count_(0) {
	EVPP_TRACE("this={}", (void*) this);
	Init();

	// When we build an EventLoop instance from an existing event_base
	// object, we will never call EventLoop::Run() method.
	// So we need to watch the task queue here.
	bool rc = watcher_->AsyncWait();
	if (!rc) {
		std::fprintf(stderr, "[EventLoop] PipeEventWatcher::AsyncWait() failed (external base)\n");
		if (auto* l = engine::GetLogger()) {
			ENGINE_LOG_CRITICAL(l, "PipeEventWatcher init failed.");
		}
		status_.store(kStopped);
		return;
	}
	status_.store(kRunning);
}

EventLoop::~EventLoop() {
	EVPP_TRACE("this={}", (void*) this);
	watcher_.reset();

	if (evbase_ != nullptr && create_evbase_myself_) {
		event_base_free(evbase_);
		evbase_ = nullptr;
	}

#ifdef H_HAVE_BOOST
	if (pending_functors_) {
		Functor* f = nullptr;
		while (pending_functors_->pop(f)) {
			delete f;
		}
	}
#endif
	delete pending_functors_;
	pending_functors_ = nullptr;
}

void EventLoop::Init() {
	EVPP_TRACE("this={}", (void*) this);
	status_.store(kInitializing);
#ifdef H_HAVE_BOOST
	const size_t kPendingFunctorCount = 1024 * 16;
	this->pending_functors_ = new boost::lockfree::queue<Functor*>(kPendingFunctorCount);
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
	this->pending_functors_ = new moodycamel::ConcurrentQueue<Functor>();
#else
	this->pending_functors_ = new std::vector<Functor>();
#endif

	tid_ = std::this_thread::get_id();	// The default thread id

	InitNotifyPipeWatcher();

	status_.store(kInitialized);
}

void EventLoop::InitNotifyPipeWatcher() {
	// Initialized task queue notify pipe watcher
	watcher_.reset(new PipeEventWatcher(this, std::bind(&EventLoop::DoPendingFunctors, this)));
	int rc = watcher_->Init();
	if (!rc) {
		std::fprintf(stderr, "[EventLoop] PipeEventWatcher::Init() failed\n");
		if (auto* l = engine::GetLogger()) {
			ENGINE_LOG_CRITICAL(l, "PipeEventWatcher init failed.");
		}
	}
	assert(rc);
}

void EventLoop::Run() {
	EVPP_TRACE("this={}", (void*) this);
	status_.store(kStarting);
	tid_ = std::this_thread::get_id();	// The actual thread id

	int rc = watcher_->AsyncWait();
	if (!rc) {
		std::fprintf(stderr, "[EventLoop] PipeEventWatcher::AsyncWait() failed\n");
		if (auto* l = engine::GetLogger()) {
			ENGINE_LOG_CRITICAL(l, "PipeEventWatcher AsyncWait failed.");
		}
	}
	assert(rc);

	// After everything have initialized, we set the status to kRunning
	status_.store(kRunning);

	// Set thread-local event base for cross-thread safety checks
	SetTlsEventBase(evbase_);

	rc = event_base_dispatch(evbase_);

	ClearTlsEventBase();

	if (rc == 1) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "event_base_dispatch error: no event registered");
	} else if (rc == -1) {
		int serrno = EVPP_ERRNO;
		ENGINE_LOG_ERROR(
			engine::GetLogger(), "event_base_dispatch error {} {}", serrno, strerror(serrno));
	}

	// Make sure watcher_ does construct, initialize and destruct in the same thread.
	watcher_.reset();
	EVPP_TRACE("this={} EventLoop stopped, tid={}",
			   (void*) this,
			   std::hash<std::thread::id>{}(std::this_thread::get_id()));

	status_.store(kStopped);
}

void EventLoop::Stop() {
	EVPP_TRACE("this={}", (void*) this);
	assert(status_.load() == kRunning);
	status_.store(kStopping);
	EVPP_TRACE("this={} EventLoop::Stop", (void*) this);
	if (IsInLoopThread()) {
		StopInLoop();
	} else {
		QueueInLoop(std::bind(&EventLoop::StopInLoop, this));
	}
}

void EventLoop::StopInLoop() {
	EVPP_TRACE("this={} EventLoop is stopping now, tid={}",
			   (void*) this,
			   std::hash<std::thread::id>{}(std::this_thread::get_id()));
	assert(status_.load() == kStopping);

	auto f = [this]() {
		for (int i = 0;; i++) {
			EVPP_TRACE("this={} calling DoPendingFunctors index={}", (void*) this, i);
			DoPendingFunctors();
			if (IsPendingQueueEmpty()) {
				break;
			}
		}
	};

	EVPP_TRACE("this={} before event_base_loopexit, we invoke DoPendingFunctors", (void*) this);

	f();

	EVPP_TRACE("this={} start event_base_loopbreak", (void*) this);
	event_base_loopbreak(evbase_);
	EVPP_TRACE("this={} after event_base_loopbreak, we invoke DoPendingFunctors", (void*) this);

	f();

	EVPP_TRACE("this={} end of StopInLoop", (void*) this);
}

void EventLoop::AfterFork() {
	int rc = event_reinit(evbase_);
	assert(rc == 0);

	if (rc != 0) {
		ENGINE_LOG_CRITICAL(engine::GetLogger(), "event_reinit failed!");
		std::exit(EXIT_FAILURE);
	}

	// We create EventLoopThread and initialize it in father process,
	// but we use it in child process.
	// If we have only one child process, everything goes well.
	//
	// But if we have multi child processes, something goes wrong.
	// Because EventLoop::watcher_ is created and initialized in father process
	// all children processes inherited father's pipe.
	//
	// When we use the pipe to do a notification in one child process
	// the notification may be received by another child process randomly.
	//
	// So we need to reinitialize the watcher_
	InitNotifyPipeWatcher();
}

InvokeTimerPtr EventLoop::RunAfter(double delay_ms, const Functor& f) {
	EVPP_TRACE("this={}", (void*) this);
	return RunAfter(Duration(static_cast<int64_t>(delay_ms * 1000000.0)), f);
}

InvokeTimerPtr EventLoop::RunAfter(double delay_ms, Functor&& f) {
	EVPP_TRACE("this={}", (void*) this);
	return RunAfter(Duration(static_cast<int64_t>(delay_ms * 1000000.0)), std::move(f));
}

InvokeTimerPtr EventLoop::RunAfter(Duration delay, const Functor& f) {
	EVPP_TRACE("this={}", (void*) this);
	std::shared_ptr<InvokeTimer> t = InvokeTimer::Create(this, delay, f, false);
	t->Start();
	return t;
}

InvokeTimerPtr EventLoop::RunAfter(Duration delay, Functor&& f) {
	EVPP_TRACE("this={}", (void*) this);
	std::shared_ptr<InvokeTimer> t = InvokeTimer::Create(this, delay, std::move(f), false);
	t->Start();
	return t;
}

evpp::InvokeTimerPtr EventLoop::RunEvery(Duration interval, const Functor& f) {
	EVPP_TRACE("this={}", (void*) this);
	std::shared_ptr<InvokeTimer> t = InvokeTimer::Create(this, interval, f, true);
	t->Start();
	return t;
}

evpp::InvokeTimerPtr EventLoop::RunEvery(Duration interval, Functor&& f) {
	EVPP_TRACE("this={}", (void*) this);
	std::shared_ptr<InvokeTimer> t = InvokeTimer::Create(this, interval, std::move(f), true);
	t->Start();
	return t;
}

void EventLoop::RunInLoop(const Functor& functor) {
	EVPP_TRACE("this={}", (void*) this);
	if (IsRunning() && IsInLoopThread()) {
		functor();
	} else {
		QueueInLoop(functor);
	}
}

void EventLoop::RunInLoop(Functor&& functor) {
	EVPP_TRACE("this={}", (void*) this);
	if (IsRunning() && IsInLoopThread()) {
		functor();
	} else {
		QueueInLoop(std::move(functor));
	}
}

void EventLoop::QueueInLoop(const Functor& cb) {
	EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
			   (void*) this,
			   pending_functor_count_.load(),
			   GetPendingQueueSize(),
			   notified_.load());
	++pending_functor_count_;
	{
#ifdef H_HAVE_BOOST
		auto f = new Functor(cb);
		while (!pending_functors_->push(f)) {
		}
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
		while (!pending_functors_->enqueue(cb)) {
		}
#else
		std::lock_guard<std::mutex> lock(mutex_);
		pending_functors_->emplace_back(cb);
#endif
	}
	EVPP_TRACE(
		"this={} queued a new Functor. pending_functor_count_={} PendingQueueSize={} notified_={}",
		(void*) this,
		pending_functor_count_.load(),
		GetPendingQueueSize(),
		notified_.load());
	if (!notified_.exchange(true)) {
		EVPP_TRACE("this={} call watcher_->Notify() notified_.exchange(true) returned false",
				   (void*) this);

		if (watcher_) {
			watcher_->Notify();
		} else {
			EVPP_TRACE("this={} status={}", (void*) this, StatusToString());
			assert(!IsRunning());
		}
	} else {
		EVPP_TRACE("this={} No need to call watcher_->Notify()", (void*) this);
	}
}

void EventLoop::QueueInLoop(Functor&& cb) {
	EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
			   (void*) this,
			   pending_functor_count_.load(),
			   GetPendingQueueSize(),
			   notified_.load());
	++pending_functor_count_;
	{
#ifdef H_HAVE_BOOST
		auto f = new Functor(std::move(cb));  // Deleted by consumer in DoPendingFunctors
		while (!pending_functors_->push(f)) {
		}
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
		while (!pending_functors_->enqueue(std::move(cb))) {
		}
#else
		std::lock_guard<std::mutex> lock(mutex_);
		pending_functors_->emplace_back(std::move(cb));
#endif
	}
	EVPP_TRACE(
		"this={} queued a new Functor. pending_functor_count_={} PendingQueueSize={} notified_={}",
		(void*) this,
		pending_functor_count_.load(),
		GetPendingQueueSize(),
		notified_.load());
	if (!notified_.exchange(true)) {
		EVPP_TRACE("this={} call watcher_->Notify() notified_.exchange(true) returned false",
				   (void*) this);
		if (watcher_) {
			watcher_->Notify();
		} else {
			EVPP_TRACE(
				"this={} watcher_ is empty, maybe we call EventLoop::QueueInLoop on a stopped "
				"EventLoop. status={}",
				(void*) this,
				StatusToString());
			assert(!IsRunning());
		}
	} else {
		EVPP_TRACE("this={} No need to call watcher_->Notify()", (void*) this);
	}
}

void EventLoop::DoPendingFunctors() {
	EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
			   (void*) this,
			   pending_functor_count_.load(),
			   GetPendingQueueSize(),
			   notified_.load());

#ifdef H_HAVE_BOOST
	notified_.store(false);
	Functor* f = nullptr;
	while (pending_functors_->pop(f)) {
		(*f)();
		delete f;
		--pending_functor_count_;
	}
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
	notified_.store(false);
	Functor f;
	while (pending_functors_->try_dequeue(f)) {
		f();
		--pending_functor_count_;
	}
#else
	std::vector<Functor> functors;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		notified_.store(false);
		pending_functors_->swap(functors);
		EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
				   (void*) this,
				   pending_functor_count_.load(),
				   GetPendingQueueSize(),
				   notified_.load());
	}
	EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
			   (void*) this,
			   pending_functor_count_.load(),
			   GetPendingQueueSize(),
			   notified_.load());
	for (size_t i = 0; i < functors.size(); ++i) {
		functors[i]();
		--pending_functor_count_;
	}
	EVPP_TRACE("this={} pending_functor_count_={} PendingQueueSize={} notified_={}",
			   (void*) this,
			   pending_functor_count_.load(),
			   GetPendingQueueSize(),
			   notified_.load());
#endif
}

size_t EventLoop::GetPendingQueueSize() {
#ifdef H_HAVE_BOOST
	return static_cast<size_t>(pending_functor_count_.load());
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
	return pending_functors_->size_approx();
#else
	std::lock_guard<std::mutex> lock(mutex_);
	return pending_functors_->size();
#endif
}

bool EventLoop::IsPendingQueueEmpty() {
#ifdef H_HAVE_BOOST
	return pending_functors_->empty();
#elif defined(H_HAVE_CAMERON314_CONCURRENTQUEUE)
	return pending_functors_->size_approx() == 0;
#else
	std::lock_guard<std::mutex> lock(mutex_);
	return pending_functors_->empty();
#endif
}

}
