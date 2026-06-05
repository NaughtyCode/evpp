#pragma once

#define ENGINE_REDIS_INTERNAL
#include "runtime/database/redis/module_access.h"
#undef ENGINE_REDIS_INTERNAL

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <concurrentqueue.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <event2/util.h>

#include "runtime/config/redis_config.h"
#include "runtime/database/redis/redis_client.h"
#include "runtime/database/redis/redis_request.h"

struct event;
struct event_base;
struct redisAsyncContext;

namespace engine {
namespace redis {

struct RedisSharedCounters {
	std::atomic<size_t> unsent_global{0};
	std::atomic<size_t> inflight_global{0};
	std::atomic<uint64_t> accepted_requests{0};
	std::atomic<uint64_t> rejected_requests{0};
	std::atomic<uint64_t> completed_requests{0};
	std::atomic<uint64_t> timed_out_requests{0};
};

class RedisClientScriptVM;

class RedisClientThread {
public:
	RedisClientThread();
	~RedisClientThread();

	RedisClientThread(const RedisClientThread&) = delete;
	RedisClientThread& operator=(const RedisClientThread&) = delete;

	bool Start(size_t index,
			   const RedisClientConfig& config,
			   std::shared_ptr<RedisSharedCounters> counters,
			   bool wait_for_initial_connect);
	void Stop();

	bool Enqueue(RedisRequest&& request);
	bool IsRunning() const;
	bool IsHealthy() const;
	size_t LoadScore() const;
	RedisWorkerStats GetStats() const;
	bool WaitForStartup(std::chrono::milliseconds timeout);
	bool WaitForInitialConnect(std::chrono::milliseconds timeout);
	bool InitialConnectSucceeded() const;

	size_t Index() const { return index_; }
	const RedisClientConfig& GetConfig() const { return config_; }

private:
	struct PendingRequest;

	void EventLoop();
	void SetupEventBase();
	void CleanupOnThread();
	void Connect();
	void ScheduleReconnect();
	void MarkStartupFinished(bool ok);
	void MarkInitialConnectFinished(bool ok);
	void MarkHealthy();

	void OnConnect(int status);
	void OnDisconnect(int status);
	void OnAuthReply(void* reply);
	void OnSelectReply(void* reply);

	void Tick();
	void DrainWakeup();
	void DrainRequests();
	void FlushUnsent();
	void CheckTimeouts();
	void CompleteAllQueued(RedisResultStatus status, const std::string& error);
	void CompleteAllUnsent(RedisResultStatus status, const std::string& error);
	void CompleteAllPending(RedisResultStatus status, const std::string& error);
	void CompleteUnsentRequest(RedisRequest&& request,
							   RedisResultStatus status,
							   std::string error);
	bool TryReserveInflight();
	void ReleaseInflight();
	void ReleaseUnsentSlot();
	void TryCompletePending(const std::shared_ptr<PendingRequest>& pending,
							RedisResult result,
							bool release_inflight);
	void Wakeup();

	static void WakeupEventCallback(evutil_socket_t fd, short events, void* arg);
	static void TickEventCallback(evutil_socket_t fd, short events, void* arg);
	static void ConnectCallback(const redisAsyncContext* context, int status);
	static void DisconnectCallback(const redisAsyncContext* context, int status);
	static void AuthCallback(redisAsyncContext* context, void* reply, void* privdata);
	static void SelectCallback(redisAsyncContext* context, void* reply, void* privdata);
	static void CommandCallback(redisAsyncContext* context, void* reply, void* privdata);

	size_t index_ = 0;
	RedisClientConfig config_;
	std::shared_ptr<RedisSharedCounters> counters_;

	std::thread thread_;
	std::atomic<bool> running_{false};
	std::atomic<bool> stopping_{false};
	std::atomic<bool> healthy_{false};
	std::atomic<size_t> queued_requests_{0};
	std::atomic<size_t> unsent_requests_count_{0};
	std::atomic<size_t> inflight_requests_count_{0};
	std::atomic<uint64_t> completed_requests_{0};
	std::atomic<uint64_t> timed_out_requests_{0};

	moodycamel::ConcurrentQueue<RedisRequest> request_queue_;
	std::deque<RedisRequest> unsent_requests_;
	std::unordered_map<uint64_t, std::shared_ptr<PendingRequest>> pending_;

	event_base* event_base_ = nullptr;
	event* wakeup_event_ = nullptr;
	event* tick_event_ = nullptr;
	evutil_socket_t wakeup_fds_[2] = {-1, -1};
	redisAsyncContext* context_ = nullptr;
	bool reconnect_scheduled_ = false;
	int reconnect_delay_ms_ = 0;
	std::chrono::steady_clock::time_point reconnect_due_;

	std::unique_ptr<RedisClientScriptVM> script_vm_;
	int64_t frame_count_ = 0;
	std::chrono::steady_clock::time_point last_frame_time_;

	std::mutex startup_mutex_;
	std::condition_variable startup_cv_;
	bool startup_finished_ = false;
	bool startup_ok_ = false;

	mutable std::mutex initial_mutex_;
	std::condition_variable initial_cv_;
	bool initial_connect_finished_ = false;
	bool initial_connect_ok_ = false;
};

}  // namespace redis
}  // namespace engine
