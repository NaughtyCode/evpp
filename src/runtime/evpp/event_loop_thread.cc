#include "runtime/evpp/event_loop.h"

#include "runtime/evpp/event_loop_thread.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/core/log/log.h"

namespace evpp {

EventLoopThread::EventLoopThread() : event_loop_(CLOUDENGINE_MEM_NEW(EventLoop)) {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} loop={}", (void*) this, (void*) event_loop_.get());
}

EventLoopThread::~EventLoopThread() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} loop={}", (void*) this, (void*) event_loop_.get());
	assert(IsStopped());
	Join();
}

bool EventLoopThread::Start(bool wait_thread_started, Functor pre, Functor post) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	status_ = kStarting;

	assert(thread_.get() == nullptr);
	try {
		thread_.reset(CLOUDENGINE_MEM_NEW(std::thread, std::bind(&EventLoopThread::Run, this, pre, post)));
	} catch (const std::system_error& e) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "EventLoopThread::Start failed to create thread: {} code={}",
						 e.what(),
						 e.code().value());
		status_ = kStopped;
		return false;
	}

	if (wait_thread_started) {
		while (status_ < kRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	return true;
}

void EventLoopThread::Run(const Functor& pre, const Functor& post) {
	engine::SetCurrentThreadName("EventLoopThread");

	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} loop={}", (void*) this, (void*) event_loop_.get());
	if (name_.empty()) {
		std::ostringstream os;
		os << "thread-" << std::this_thread::get_id();
		name_ = os.str();
	}


	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} loop={} execute pre functor.",
					 (void*) this,
					 (void*) event_loop_.get());
	auto fn = [this, pre]() {
		status_ = kRunning;
		if (pre) {
			auto rc = pre();
			if (rc != kOK) {
				event_loop_->Stop();
			}
		}
	};
	event_loop_->QueueInLoop(std::move(fn));
	event_loop_->Run();

	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} loop={} execute post functor.",
					 (void*) this,
					 (void*) event_loop_.get());
	if (post) {
		post();
	}

	assert(event_loop_->IsStopped());
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} loop={} EventLoopThread stopped",
					 (void*) this,
					 (void*) event_loop_.get());
	status_ = kStopped;
}

void EventLoopThread::Stop(bool wait_thread_exit) {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} loop={} wait_thread_exit={}",
					 (void*) this,
					 (void*) event_loop_.get(),
					 wait_thread_exit);
	assert(status_ == kRunning && IsRunning());
	status_ = kStopping;
	event_loop_->Stop();

	if (wait_thread_exit) {
		while (!IsStopped()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} loop={} thread stopped.",
						 (void*) this,
						 (void*) event_loop_.get());
		Join();
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} loop={} thread totally stopped.",
						 (void*) this,
						 (void*) event_loop_.get());
	}
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} loop={}", (void*) this, (void*) event_loop_.get());
}

void EventLoopThread::Join() {
	// To avoid multi other threads call Join simultaneously
	std::lock_guard<std::mutex> guard(mutex_);
	if (thread_ && thread_->joinable()) {
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} loop={} thread={} joinable",
						 (void*) this,
						 (void*) event_loop_.get(),
						 (void*) thread_.get());
		try {
			thread_->join();
		} catch (const std::system_error& e) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "Caught a system_error: {} code={}",
							 e.what(),
							 e.code().value());
		}
		thread_.reset();
	}
}

void EventLoopThread::set_name(const std::string& n) {
	name_ = n;
}

const std::string& EventLoopThread::name() const {
	return name_;
}


EventLoop* EventLoopThread::loop() const {
	return event_loop_.get();
}


struct event_base* EventLoopThread::event_base() {
	return loop()->event_base();
}

std::thread::id EventLoopThread::tid() const {
	if (thread_) {
		return thread_->get_id();
	}

	return std::thread::id();
}

bool EventLoopThread::IsRunning() const {
	// Using event_loop_->IsRunning() is more exact to query where thread is
	// running or not instead of status_ == kRunning
	//
	// Because in some particular circumstances,
	// when status_==kRunning and event_loop_::running_ == false,
	// the application will broke down
	return event_loop_->IsRunning();
}

void EventLoopThread::AfterFork() {
	loop()->AfterFork();
}

}
