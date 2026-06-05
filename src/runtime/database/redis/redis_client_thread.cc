#include "runtime/database/redis/redis_client_thread.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <async.h>
#include <hiredis.h>
#include <adapters/libevent.h>
#include <event2/event.h>

#include "runtime/core/log/log.h"
#include "runtime/database/redis/bind/redis_bind.h"
#include "runtime/database/redis/redis_client_script_vm.h"
#include "runtime/database/redis/redis_connection.h"
#include "runtime/database/redis/redis_value.h"

namespace engine {
namespace redis {

namespace {

constexpr size_t kDrainBatch = 1024;

/* Detects null replies and Redis error replies from hiredis callbacks. */
bool IsReplyError(void* reply_ptr, std::string* error) {
	auto* reply = static_cast<redisReply*>(reply_ptr);
	if (!reply) {
		if (error) *error = "redis returned null reply";
		return true;
	}
	if (reply->type == REDIS_REPLY_ERROR) {
		if (error) {
			*error = reply->str ? std::string(reply->str, reply->len) : "redis command error";
		}
		return true;
	}
	return false;
}

}  // namespace

struct RedisClientThread::PendingRequest {
	RedisRequest request;
	RedisClientThread* owner = nullptr;
	std::chrono::steady_clock::time_point sent_at;
	std::atomic<bool> completed{false};
};

/* Creates a Redis worker thread object before it owns any thread resources. */
RedisClientThread::RedisClientThread() = default;

/* Ensures the worker thread is stopped before object destruction. */
RedisClientThread::~RedisClientThread() {
	Stop();
}

/* Starts the event-loop thread and optionally waits for the first Redis connection. */
bool RedisClientThread::Start(size_t index,
							  const RedisClientConfig& config,
							  std::shared_ptr<RedisSharedCounters> counters,
							  bool wait_for_initial_connect) {
	if (running_.load(std::memory_order_acquire)) {
		return true;
	}

	index_ = index;
	config_ = config;
	counters_ = std::move(counters);
	stopping_.store(false, std::memory_order_release);
	healthy_.store(false, std::memory_order_release);
	startup_finished_ = false;
	startup_ok_ = false;
	initial_connect_finished_ = false;
	initial_connect_ok_ = false;
	reconnect_delay_ms_ = config_.reconnect.initial_delay_ms;

	try {
		running_.store(true, std::memory_order_release);
		thread_ = std::thread(&RedisClientThread::EventLoop, this);
	} catch (const std::exception& e) {
		running_.store(false, std::memory_order_release);
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: failed to start: {}", index_, e.what());
		return false;
	}

	const auto startup_timeout = std::chrono::milliseconds(config_.connection.connect_timeout_ms);
	if (!WaitForStartup(startup_timeout)) {
		ENGINE_LOG_ERROR(GetLogger(),
						 "RedisClientThread[{}]: startup timed out after {}ms",
						 index_,
						 config_.connection.connect_timeout_ms);
		Stop();
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(startup_mutex_);
		if (!startup_ok_) {
			ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: startup failed", index_);
			Stop();
			return false;
		}
	}

	if (wait_for_initial_connect) {
		const auto timeout = std::chrono::milliseconds(config_.connection.connect_timeout_ms);
		if (!WaitForInitialConnect(timeout)) {
			ENGINE_LOG_ERROR(GetLogger(),
							 "RedisClientThread[{}]: initial connect timed out after {}ms",
							 index_,
							 config_.connection.connect_timeout_ms);
			Stop();
			return false;
		}
		if (!InitialConnectSucceeded()) {
			ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: initial connect failed", index_);
			Stop();
			return false;
		}
	}

	return true;
}

/* Requests event-loop shutdown and joins the worker thread. */
void RedisClientThread::Stop() {
	if (!running_.load(std::memory_order_acquire) && !thread_.joinable()) {
		return;
	}
	stopping_.store(true, std::memory_order_release);
	running_.store(false, std::memory_order_release);
	Wakeup();
	if (thread_.joinable()) {
		thread_.join();
	}
}

/* Enqueues an accepted Redis request and wakes the event loop. */
bool RedisClientThread::Enqueue(RedisRequest&& request) {
	if (!running_.load(std::memory_order_acquire)) {
		return false;
	}
	queued_requests_.fetch_add(1, std::memory_order_acq_rel);
	if (!request_queue_.enqueue(std::move(request))) {
		queued_requests_.fetch_sub(1, std::memory_order_acq_rel);
		return false;
	}
	Wakeup();
	return true;
}

/* Returns whether the worker event-loop thread is active. */
bool RedisClientThread::IsRunning() const {
	return running_.load(std::memory_order_acquire);
}

/* Returns whether the worker has a ready Redis connection. */
bool RedisClientThread::IsHealthy() const {
	return healthy_.load(std::memory_order_acquire);
}

/* Computes the worker load used for least-loaded routing. */
size_t RedisClientThread::LoadScore() const {
	return queued_requests_.load(std::memory_order_acquire) +
		   unsent_requests_count_.load(std::memory_order_acquire) +
		   inflight_requests_count_.load(std::memory_order_acquire);
}

/* Captures a per-worker stats snapshot from atomic counters. */
RedisWorkerStats RedisClientThread::GetStats() const {
	RedisWorkerStats stats;
	stats.worker_index = index_;
	stats.running = IsRunning();
	stats.healthy = IsHealthy();
	stats.queued_requests = queued_requests_.load(std::memory_order_acquire);
	stats.unsent_requests = unsent_requests_count_.load(std::memory_order_acquire);
	stats.inflight_requests = inflight_requests_count_.load(std::memory_order_acquire);
	stats.completed_requests = completed_requests_.load(std::memory_order_relaxed);
	stats.timed_out_requests = timed_out_requests_.load(std::memory_order_relaxed);
	return stats;
}

/* Waits until the worker event loop has completed startup initialization. */
bool RedisClientThread::WaitForStartup(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> lock(startup_mutex_);
	return startup_cv_.wait_for(lock, timeout, [this] { return startup_finished_; });
}

/* Waits until the first Redis connection attempt has succeeded or failed. */
bool RedisClientThread::WaitForInitialConnect(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> lock(initial_mutex_);
	return initial_cv_.wait_for(lock, timeout, [this] { return initial_connect_finished_; });
}

/* Returns the result of the initial Redis connection attempt. */
bool RedisClientThread::InitialConnectSucceeded() const {
	std::lock_guard<std::mutex> lock(initial_mutex_);
	return initial_connect_finished_ && initial_connect_ok_;
}

/* Runs the libevent loop, script VM, Redis connection, and worker tick cycle. */
void RedisClientThread::EventLoop() {
	SetCurrentThreadName("RedisClientThread");
	try {
		SetupEventBase();
		script_vm_ = std::make_unique<RedisClientScriptVM>();
		script_vm_->RegisterSubsystemObjects(this, index_);
		script::ExportRedis(*script_vm_, {
			script_vm_->GetAsyncDispatcher(),
			index_,
			config_.queue.dispatch_batch_size
		});
		script_vm_->SetImportPath(config_.script.redis_scripts_dir);
		if (config_.script.auto_load) {
			script_vm_->DoDirectory(config_.script.redis_scripts_dir);
		}
		script_vm_->InitScript();

		MarkStartupFinished(true);
		Connect();
		while (!stopping_.load(std::memory_order_acquire)) {
			event_base_loop(event_base_, EVLOOP_ONCE);
		}
	} catch (const std::exception& e) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: event loop error: {}", index_, e.what());
	} catch (...) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: unknown event loop error", index_);
	}

	MarkStartupFinished(false);
	CleanupOnThread();
	healthy_.store(false, std::memory_order_release);
	running_.store(false, std::memory_order_release);
	MarkInitialConnectFinished(false);
}

/* Allocates the libevent base, wakeup socketpair, and periodic tick event. */
void RedisClientThread::SetupEventBase() {
	event_base_ = event_base_new();
	if (!event_base_) {
		throw std::runtime_error("event_base_new failed");
	}

	int socket_family = AF_INET;
#ifdef AF_UNIX
	socket_family = AF_UNIX;
#endif
	if (evutil_socketpair(socket_family, SOCK_STREAM, 0, wakeup_fds_) != 0 &&
		evutil_socketpair(AF_INET, SOCK_STREAM, 0, wakeup_fds_) != 0) {
		throw std::runtime_error("evutil_socketpair failed");
	}
	evutil_make_socket_nonblocking(wakeup_fds_[0]);
	evutil_make_socket_nonblocking(wakeup_fds_[1]);

	wakeup_event_ = event_new(event_base_, wakeup_fds_[1], EV_READ | EV_PERSIST,
							  &RedisClientThread::WakeupEventCallback, this);
	if (!wakeup_event_) {
		throw std::runtime_error("event_new wakeup failed");
	}
	event_add(wakeup_event_, nullptr);

	tick_event_ = event_new(event_base_, -1, EV_PERSIST,
							&RedisClientThread::TickEventCallback, this);
	if (!tick_event_) {
		throw std::runtime_error("event_new tick failed");
	}
	const int fps = std::max(1, config_.thread.main_loop_fps);
	timeval tv{};
	tv.tv_sec = 0;
	tv.tv_usec = 1000000 / fps;
	event_add(tick_event_, &tv);
	last_frame_time_ = std::chrono::steady_clock::now();
}

/* Completes outstanding work and releases Redis, script, socket, and event resources. */
void RedisClientThread::CleanupOnThread() {
	CompleteAllQueued(RedisResultStatus::kShutdown, "redis worker shutting down");
	CompleteAllUnsent(RedisResultStatus::kShutdown, "redis worker shutting down");
	CompleteAllPending(RedisResultStatus::kShutdown, "redis worker shutting down");

	if (context_) {
		redisAsyncDisconnect(context_);
		context_ = nullptr;
	}
	if (script_vm_) {
		script_vm_->DestroyScript();
		script_vm_->ShutdownAsyncDispatcher();
		script_vm_.reset();
	}
	if (tick_event_) {
		event_free(tick_event_);
		tick_event_ = nullptr;
	}
	if (wakeup_event_) {
		event_free(wakeup_event_);
		wakeup_event_ = nullptr;
	}
	if (wakeup_fds_[0] != -1) {
		evutil_closesocket(wakeup_fds_[0]);
		wakeup_fds_[0] = -1;
	}
	if (wakeup_fds_[1] != -1) {
		evutil_closesocket(wakeup_fds_[1]);
		wakeup_fds_[1] = -1;
	}
	if (event_base_) {
		event_base_free(event_base_);
		event_base_ = nullptr;
	}
}

/* Opens a hiredis async connection and attaches it to this worker event loop. */
void RedisClientThread::Connect() {
	if (stopping_.load(std::memory_order_acquire) || context_) {
		return;
	}
	reconnect_scheduled_ = false;
	healthy_.store(false, std::memory_order_release);

	context_ = redisAsyncConnect(config_.connection.host.c_str(), config_.connection.port);
	if (!context_) {
		MarkInitialConnectFinished(false);
		ScheduleReconnect();
		return;
	}
	context_->data = this;
	if (context_->err) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: connect error: {}",
						 index_, context_->errstr);
		redisAsyncFree(context_);
		context_ = nullptr;
		MarkInitialConnectFinished(false);
		ScheduleReconnect();
		return;
	}
	if (config_.connection.keepalive) {
		redisEnableKeepAlive(&context_->c);
	}
	if (redisLibeventAttach(context_, event_base_) != REDIS_OK ||
		redisAsyncSetConnectCallback(context_, &RedisClientThread::ConnectCallback) != REDIS_OK ||
		redisAsyncSetDisconnectCallback(context_, &RedisClientThread::DisconnectCallback) != REDIS_OK) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: failed to attach hiredis async context",
						 index_);
		redisAsyncFree(context_);
		context_ = nullptr;
		MarkInitialConnectFinished(false);
		ScheduleReconnect();
	}
}

/* Schedules the next reconnect attempt using the configured backoff policy. */
void RedisClientThread::ScheduleReconnect() {
	if (!config_.reconnect.enabled || stopping_.load(std::memory_order_acquire)) {
		return;
	}
	ENGINE_LOG_WARN(GetLogger(),
					"RedisClientThread[{}]: scheduling reconnect in {}ms",
					index_,
					reconnect_delay_ms_);
	reconnect_scheduled_ = true;
	reconnect_due_ = std::chrono::steady_clock::now() +
					 std::chrono::milliseconds(reconnect_delay_ms_);
	reconnect_delay_ms_ = std::min(config_.reconnect.max_delay_ms,
								   reconnect_delay_ms_ *
									   std::max(1, config_.reconnect.backoff_multiplier));
}

/* Publishes the event-loop startup result to the starting thread. */
void RedisClientThread::MarkStartupFinished(bool ok) {
	std::lock_guard<std::mutex> lock(startup_mutex_);
	if (!startup_finished_) {
		startup_finished_ = true;
		startup_ok_ = ok;
		startup_cv_.notify_all();
	}
}

/* Publishes the initial Redis connection result to waiters. */
void RedisClientThread::MarkInitialConnectFinished(bool ok) {
	std::lock_guard<std::mutex> lock(initial_mutex_);
	if (!initial_connect_finished_) {
		initial_connect_finished_ = true;
		initial_connect_ok_ = ok;
		initial_cv_.notify_all();
	}
}

/* Marks the connection healthy and resumes flushing queued Redis commands. */
void RedisClientThread::MarkHealthy() {
	reconnect_delay_ms_ = config_.reconnect.initial_delay_ms;
	healthy_.store(true, std::memory_order_release);
	MarkInitialConnectFinished(true);
	FlushUnsent();
}

/* Handles the hiredis connect callback and starts AUTH/SELECT handshakes. */
void RedisClientThread::OnConnect(int status) {
	if (status != REDIS_OK) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: connect failed", index_);
		healthy_.store(false, std::memory_order_release);
		MarkInitialConnectFinished(false);
		if (context_) {
			redisAsyncDisconnect(context_);
		} else {
			ScheduleReconnect();
		}
		return;
	}

	if (!config_.connection.password.empty()) {
		int rc = REDIS_ERR;
		if (!config_.connection.username.empty()) {
			const char* argv[] = {"AUTH", config_.connection.username.c_str(),
								  config_.connection.password.c_str()};
			size_t argvlen[] = {4, config_.connection.username.size(),
								config_.connection.password.size()};
			rc = redisAsyncCommandArgv(context_, &RedisClientThread::AuthCallback,
									   this, 3, argv, argvlen);
		} else {
			const char* argv[] = {"AUTH", config_.connection.password.c_str()};
			size_t argvlen[] = {4, config_.connection.password.size()};
			rc = redisAsyncCommandArgv(context_, &RedisClientThread::AuthCallback,
									   this, 2, argv, argvlen);
		}
		if (rc != REDIS_OK) {
			OnAuthReply(nullptr);
		}
		return;
	}

	if (config_.connection.database != 0) {
		std::string db = std::to_string(config_.connection.database);
		const char* argv[] = {"SELECT", db.c_str()};
		size_t argvlen[] = {6, db.size()};
		if (redisAsyncCommandArgv(context_, &RedisClientThread::SelectCallback,
								  this, 2, argv, argvlen) != REDIS_OK) {
			OnSelectReply(nullptr);
		}
		return;
	}

	MarkHealthy();
}

/* Handles Redis disconnects, request completion, and reconnect scheduling. */
void RedisClientThread::OnDisconnect(int status) {
	healthy_.store(false, std::memory_order_release);
	context_ = nullptr;
	if (!stopping_.load(std::memory_order_acquire)) {
		CompleteAllPending(RedisResultStatus::kConnectionError,
						   RedisConnectionErrorString(status, nullptr));
		if (!config_.reconnect.queue_while_disconnected) {
			CompleteAllQueued(RedisResultStatus::kConnectionError, "redis disconnected");
			CompleteAllUnsent(RedisResultStatus::kConnectionError, "redis disconnected");
		}
		ScheduleReconnect();
	}
}

/* Handles AUTH replies and continues the connection handshake. */
void RedisClientThread::OnAuthReply(void* reply) {
	std::string error;
	if (IsReplyError(reply, &error)) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: AUTH failed: {}", index_, error);
		healthy_.store(false, std::memory_order_release);
		MarkInitialConnectFinished(false);
		if (context_) redisAsyncDisconnect(context_);
		return;
	}
	if (config_.connection.database != 0) {
		std::string db = std::to_string(config_.connection.database);
		const char* argv[] = {"SELECT", db.c_str()};
		size_t argvlen[] = {6, db.size()};
		if (redisAsyncCommandArgv(context_, &RedisClientThread::SelectCallback,
								  this, 2, argv, argvlen) != REDIS_OK) {
			OnSelectReply(nullptr);
		}
		return;
	}
	MarkHealthy();
}

/* Handles SELECT replies and marks the worker healthy when database selection succeeds. */
void RedisClientThread::OnSelectReply(void* reply) {
	std::string error;
	if (IsReplyError(reply, &error)) {
		ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: SELECT failed: {}", index_, error);
		healthy_.store(false, std::memory_order_release);
		MarkInitialConnectFinished(false);
		if (context_) redisAsyncDisconnect(context_);
		return;
	}
	MarkHealthy();
}

/* Advances reconnects, queues, timeouts, script callbacks, and script updates. */
void RedisClientThread::Tick() {
	if (reconnect_scheduled_ && !context_ &&
		std::chrono::steady_clock::now() >= reconnect_due_) {
		Connect();
	}
	DrainRequests();
	FlushUnsent();
	CheckTimeouts();
	if (script_vm_) {
		script_vm_->DispatchAsyncResults(config_.queue.dispatch_batch_size);
		script_vm_->UpdateScript();
	}
	++frame_count_;
}

/* Drains the wakeup socket so the event remains edge-safe across bursts. */
void RedisClientThread::DrainWakeup() {
	char buffer[256];
	while (true) {
		const int rc = recv(wakeup_fds_[1], buffer, sizeof(buffer), 0);
		if (rc <= 0) break;
		if (rc < static_cast<int>(sizeof(buffer))) break;
	}
}

/* Moves accepted requests from the MPSC queue into the worker-owned unsent queue. */
void RedisClientThread::DrainRequests() {
	size_t drained = 0;
	RedisRequest request;
	while (drained++ < kDrainBatch && request_queue_.try_dequeue(request)) {
		queued_requests_.fetch_sub(1, std::memory_order_acq_rel);
		const auto now = std::chrono::steady_clock::now();
		if (stopping_.load(std::memory_order_acquire)) {
			CompleteUnsentRequest(std::move(request), RedisResultStatus::kShutdown,
								  "redis worker shutting down");
			continue;
		}
		if (request.deadline <= now) {
			CompleteUnsentRequest(std::move(request), RedisResultStatus::kTimeout,
								  "redis command timed out before drain");
			timed_out_requests_.fetch_add(1, std::memory_order_relaxed);
			counters_->timed_out_requests.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		if (!healthy_.load(std::memory_order_acquire) &&
			!config_.reconnect.queue_while_disconnected) {
			CompleteUnsentRequest(std::move(request), RedisResultStatus::kConnectionError,
								  "redis worker is disconnected");
			continue;
		}
		unsent_requests_count_.fetch_add(1, std::memory_order_acq_rel);
		unsent_requests_.push_back(std::move(request));
	}
}

/* Reserves a global inflight slot before sending a command to Redis. */
bool RedisClientThread::TryReserveInflight() {
	size_t current = counters_->inflight_global.load(std::memory_order_acquire);
	while (true) {
		if (current >= config_.queue.max_inflight) {
			return false;
		}
		if (counters_->inflight_global.compare_exchange_weak(
				current, current + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
			inflight_requests_count_.fetch_add(1, std::memory_order_acq_rel);
			return true;
		}
	}
}

/* Releases one global and per-worker inflight slot. */
void RedisClientThread::ReleaseInflight() {
	counters_->inflight_global.fetch_sub(1, std::memory_order_acq_rel);
	inflight_requests_count_.fetch_sub(1, std::memory_order_acq_rel);
}

/* Releases one global unsent slot after a request leaves the unsent state. */
void RedisClientThread::ReleaseUnsentSlot() {
	counters_->unsent_global.fetch_sub(1, std::memory_order_acq_rel);
}

/* Sends ready unsent commands to Redis while respecting the inflight limit. */
void RedisClientThread::FlushUnsent() {
	if (!healthy_.load(std::memory_order_acquire) || !context_) {
		return;
	}
	while (!unsent_requests_.empty()) {
		auto now = std::chrono::steady_clock::now();
		if (unsent_requests_.front().deadline <= now) {
			RedisRequest request = std::move(unsent_requests_.front());
			unsent_requests_.pop_front();
			unsent_requests_count_.fetch_sub(1, std::memory_order_acq_rel);
			CompleteUnsentRequest(std::move(request), RedisResultStatus::kTimeout,
								  "redis command timed out before send");
			timed_out_requests_.fetch_add(1, std::memory_order_relaxed);
			counters_->timed_out_requests.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		if (!TryReserveInflight()) {
			return;
		}

		RedisRequest request = std::move(unsent_requests_.front());
		unsent_requests_.pop_front();
		unsent_requests_count_.fetch_sub(1, std::memory_order_acq_rel);
		ReleaseUnsentSlot();

		auto pending = std::make_shared<PendingRequest>();
		pending->request = std::move(request);
		pending->owner = this;
		pending->sent_at = std::chrono::steady_clock::now();
		auto* token = new std::shared_ptr<PendingRequest>(pending);
		const uint64_t request_id = pending->request.request_id;
		pending_[request_id] = pending;

		std::vector<const char*> argv;
		std::vector<size_t> argvlen;
		argv.reserve(pending->request.argv.size());
		argvlen.reserve(pending->request.argv.size());
		for (const auto& arg : pending->request.argv) {
			argv.push_back(arg.data());
			argvlen.push_back(arg.size());
		}
		const int rc = redisAsyncCommandArgv(context_, &RedisClientThread::CommandCallback,
											 token,
											 static_cast<int>(argv.size()),
											 argv.data(),
											 argvlen.data());
		if (rc != REDIS_OK) {
			pending_.erase(request_id);
			RedisResult result;
			result.status = RedisResultStatus::kConnectionError;
			result.error = "redisAsyncCommandArgv failed";
			result.request_id = request_id;
			TryCompletePending(pending, std::move(result), true);
			delete token;
			continue;
		}
	}
}

/* Expires unsent and pending requests whose deadlines have passed. */
void RedisClientThread::CheckTimeouts() {
	const auto now = std::chrono::steady_clock::now();
	bool released_inflight = false;

	auto unsent_it = unsent_requests_.begin();
	while (unsent_it != unsent_requests_.end()) {
		if (unsent_it->deadline > now) {
			++unsent_it;
			continue;
		}
		RedisRequest request = std::move(*unsent_it);
		unsent_it = unsent_requests_.erase(unsent_it);
		unsent_requests_count_.fetch_sub(1, std::memory_order_acq_rel);
		CompleteUnsentRequest(std::move(request), RedisResultStatus::kTimeout,
							  "redis command timed out");
		timed_out_requests_.fetch_add(1, std::memory_order_relaxed);
		counters_->timed_out_requests.fetch_add(1, std::memory_order_relaxed);
	}

	for (auto it = pending_.begin(); it != pending_.end();) {
		auto pending = it->second;
		if (pending->request.deadline > now) {
			++it;
			continue;
		}
		it = pending_.erase(it);
		RedisResult result;
		result.status = RedisResultStatus::kTimeout;
		result.error = "redis command timed out";
		result.request_id = pending->request.request_id;
		TryCompletePending(pending, std::move(result), true);
		released_inflight = true;
		timed_out_requests_.fetch_add(1, std::memory_order_relaxed);
		counters_->timed_out_requests.fetch_add(1, std::memory_order_relaxed);
	}
	if (released_inflight) {
		FlushUnsent();
	}
}

/* Completes every request still waiting in the cross-thread queue. */
void RedisClientThread::CompleteAllQueued(RedisResultStatus status, const std::string& error) {
	RedisRequest request;
	while (request_queue_.try_dequeue(request)) {
		queued_requests_.fetch_sub(1, std::memory_order_acq_rel);
		CompleteUnsentRequest(std::move(request), status, error);
	}
}

/* Completes every request retained in the unsent queue. */
void RedisClientThread::CompleteAllUnsent(RedisResultStatus status, const std::string& error) {
	while (!unsent_requests_.empty()) {
		RedisRequest request = std::move(unsent_requests_.front());
		unsent_requests_.pop_front();
		unsent_requests_count_.fetch_sub(1, std::memory_order_acq_rel);
		CompleteUnsentRequest(std::move(request), status, error);
	}
}

/* Completes every in-flight Redis command still waiting for a reply. */
void RedisClientThread::CompleteAllPending(RedisResultStatus status, const std::string& error) {
	for (auto& [id, pending] : pending_) {
		RedisResult result;
		result.status = status;
		result.error = error;
		result.request_id = id;
		TryCompletePending(pending, std::move(result), true);
	}
	pending_.clear();
}

/* Completes a request that never reached Redis and invokes its completion safely. */
void RedisClientThread::CompleteUnsentRequest(RedisRequest&& request,
											  RedisResultStatus status,
											  std::string error) {
	ReleaseUnsentSlot();
	RedisResult result;
	result.status = status;
	result.success = status == RedisResultStatus::kOk;
	result.error = std::move(error);
	result.request_id = request.request_id;
	if (request.accepted_at.time_since_epoch().count() != 0) {
		result.elapsed_ms = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - request.accepted_at)
				.count());
	}
	auto completion = std::move(request.completion);
	if (completion) {
		try {
			completion(std::move(result));
		} catch (const std::exception& e) {
			ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: completion error: {}",
							 index_, e.what());
		}
	}
	completed_requests_.fetch_add(1, std::memory_order_relaxed);
	counters_->completed_requests.fetch_add(1, std::memory_order_relaxed);
}

/* Completes an in-flight request exactly once and records elapsed/slow-command data. */
void RedisClientThread::TryCompletePending(const std::shared_ptr<PendingRequest>& pending,
										   RedisResult result,
										   bool release_inflight) {
	if (!pending || pending->completed.exchange(true, std::memory_order_acq_rel)) {
		return;
	}
	if (release_inflight) {
		ReleaseInflight();
	}
	result.request_id = pending->request.request_id;
	result.success = result.status == RedisResultStatus::kOk;
	const auto now = std::chrono::steady_clock::now();
	if (pending->request.accepted_at.time_since_epoch().count() != 0) {
		result.elapsed_ms = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now - pending->request.accepted_at)
				.count());
	}
	if (config_.log.enabled) {
		const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - pending->sent_at).count();
		if (elapsed_ms >= config_.log.slow_command_ms && !pending->request.argv.empty()) {
			const std::string& trace_tag = pending->request.options.trace_tag;
			ENGINE_LOG_WARN(GetLogger(),
							"RedisClientThread[{}]: slow redis command [{}] trace=[{}] took {}ms",
							index_,
							pending->request.argv.front(),
							trace_tag,
							elapsed_ms);
		}
	}
	auto completion = std::move(pending->request.completion);
	if (completion) {
		try {
			completion(std::move(result));
		} catch (const std::exception& e) {
			ENGINE_LOG_ERROR(GetLogger(), "RedisClientThread[{}]: completion error: {}",
							 index_, e.what());
		}
	}
	completed_requests_.fetch_add(1, std::memory_order_relaxed);
	counters_->completed_requests.fetch_add(1, std::memory_order_relaxed);
}

/* Wakes the libevent loop from another thread. */
void RedisClientThread::Wakeup() {
	if (wakeup_fds_[0] == -1) {
		return;
	}
	const char byte = 1;
	send(wakeup_fds_[0], &byte, 1, 0);
}

/* Handles wakeup socket readability and runs one worker tick. */
void RedisClientThread::WakeupEventCallback(evutil_socket_t, short, void* arg) {
	auto* self = static_cast<RedisClientThread*>(arg);
	self->DrainWakeup();
	self->Tick();
}

/* Handles periodic timer events for reconnects, flushing, and script updates. */
void RedisClientThread::TickEventCallback(evutil_socket_t, short, void* arg) {
	auto* self = static_cast<RedisClientThread*>(arg);
	self->Tick();
}

/* Bridges hiredis connect callbacks back to the worker instance. */
void RedisClientThread::ConnectCallback(const redisAsyncContext* context, int status) {
	auto* self = static_cast<RedisClientThread*>(context ? context->data : nullptr);
	if (self) {
		self->OnConnect(status);
	}
}

/* Bridges hiredis disconnect callbacks back to the worker instance. */
void RedisClientThread::DisconnectCallback(const redisAsyncContext* context, int status) {
	auto* self = static_cast<RedisClientThread*>(context ? context->data : nullptr);
	if (self) {
		self->OnDisconnect(status);
	}
}

/* Bridges AUTH replies back to the worker connection handshake. */
void RedisClientThread::AuthCallback(redisAsyncContext*, void* reply, void* privdata) {
	auto* self = static_cast<RedisClientThread*>(privdata);
	if (self) {
		self->OnAuthReply(reply);
	}
}

/* Bridges SELECT replies back to the worker connection handshake. */
void RedisClientThread::SelectCallback(redisAsyncContext*, void* reply, void* privdata) {
	auto* self = static_cast<RedisClientThread*>(privdata);
	if (self) {
		self->OnSelectReply(reply);
	}
}

/* Converts hiredis command replies into RedisResult and completes the pending request. */
void RedisClientThread::CommandCallback(redisAsyncContext*, void* reply, void* privdata) {
	std::unique_ptr<std::shared_ptr<PendingRequest>> token(
		static_cast<std::shared_ptr<PendingRequest>*>(privdata));
	if (!token || !*token) {
		return;
	}
	auto pending = *token;
	auto* self = pending->owner;
	RedisResult result;
	result.request_id = pending->request.request_id;
	if (!reply) {
		result.status = RedisResultStatus::kConnectionError;
		result.error = "redis returned null reply";
	} else {
		auto* redis_reply = static_cast<redisReply*>(reply);
		if (redis_reply->type == REDIS_REPLY_ERROR) {
			result.status = RedisResultStatus::kCommandError;
			result.error = redis_reply->str
				? std::string(redis_reply->str, redis_reply->len)
				: "redis command error";
		} else {
			result.status = RedisResultStatus::kOk;
			result.value = RedisValueFromReply(redis_reply);
		}
	}

	if (self) {
		self->pending_.erase(pending->request.request_id);
		self->TryCompletePending(pending, std::move(result), true);
		self->FlushUnsent();
	}
}

}  // namespace redis
}  // namespace engine
