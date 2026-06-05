#include "runtime/database/redis/redis_client_thread_group.h"

#include <algorithm>
#include <chrono>
#include <limits>

#include "runtime/core/log/log.h"

namespace engine {
namespace redis {

namespace {

uint64_t Fnv1a64(const std::string& value) {
	uint64_t hash = 14695981039346656037ull;
	for (unsigned char ch : value) {
		hash ^= ch;
		hash *= 1099511628211ull;
	}
	return hash;
}

}  // namespace

RedisClientThreadGroup::RedisClientThreadGroup()
	: counters_(std::make_shared<RedisSharedCounters>()) {}

RedisClientThreadGroup::~RedisClientThreadGroup() {
	Stop();
}

bool RedisClientThreadGroup::Start(const RedisClientConfig& config,
								   bool wait_for_initial_connect) {
	config_ = config;
	counters_ = std::make_shared<RedisSharedCounters>();
	workers_.clear();
	workers_.reserve(config.thread.thread_count);

	for (size_t i = 0; i < config.thread.thread_count; ++i) {
		auto worker = std::make_unique<RedisClientThread>();
		if (!worker->Start(i, config, counters_, false)) {
			ENGINE_LOG_ERROR(GetLogger(), "RedisClientThreadGroup: worker {} failed to start", i);
			Stop();
			return false;
		}
		workers_.push_back(std::move(worker));
	}

	if (wait_for_initial_connect) {
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(config.connection.connect_timeout_ms);
		for (const auto& worker : workers_) {
			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline) {
				ENGINE_LOG_ERROR(GetLogger(),
								 "RedisClientThreadGroup: initial connect timed out");
				Stop();
				return false;
			}
			if (!worker->WaitForInitialConnect(
					std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)) ||
				!worker->InitialConnectSucceeded()) {
				ENGINE_LOG_ERROR(GetLogger(),
								 "RedisClientThreadGroup: worker {} initial connect failed",
								 worker->Index());
				Stop();
				return false;
			}
		}
	}

	return true;
}

void RedisClientThreadGroup::Stop() {
	for (auto& worker : workers_) {
		if (worker) {
			worker->Stop();
		}
	}
	workers_.clear();
}

bool RedisClientThreadGroup::ReserveUnsentSlot() {
	size_t current = counters_->unsent_global.load(std::memory_order_acquire);
	while (true) {
		if (current >= config_.queue.request_queue_size) {
			return false;
		}
		if (counters_->unsent_global.compare_exchange_weak(
				current, current + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
			return true;
		}
	}
}

void RedisClientThreadGroup::ReleaseUnsentSlot() {
	counters_->unsent_global.fetch_sub(1, std::memory_order_acq_rel);
}

size_t RedisClientThreadGroup::ChooseWorker(bool has_routing_key,
											const std::string& routing_key,
											RedisSubmitStatus& rejected_status,
											std::string& error) {
	if (workers_.empty()) {
		rejected_status = RedisSubmitStatus::kNotRunning;
		error = "redis client has no workers";
		return static_cast<size_t>(-1);
	}

	if (has_routing_key) {
		size_t index = static_cast<size_t>(Fnv1a64(routing_key) % workers_.size());
		auto& worker = workers_[index];
		if (worker->IsRunning() &&
			(worker->IsHealthy() || config_.reconnect.queue_while_disconnected)) {
			return index;
		}
		rejected_status = worker->IsRunning()
			? RedisSubmitStatus::kDisconnected
			: RedisSubmitStatus::kNotRunning;
		error = "target redis worker is not healthy";
		return static_cast<size_t>(-1);
	}

	size_t best_index = static_cast<size_t>(-1);
	size_t best_score = (std::numeric_limits<size_t>::max)();
	const size_t start = round_robin_.fetch_add(1, std::memory_order_relaxed);
	for (size_t i = 0; i < workers_.size(); ++i) {
		const size_t index = (start + i) % workers_.size();
		auto& worker = workers_[index];
		if (!worker->IsRunning() || !worker->IsHealthy()) {
			continue;
		}
		const size_t score = worker->LoadScore();
		if (score < best_score) {
			best_score = score;
			best_index = index;
		}
	}
	if (best_index != static_cast<size_t>(-1)) {
		return best_index;
	}

	if (config_.reconnect.queue_while_disconnected) {
		for (size_t i = 0; i < workers_.size(); ++i) {
			const size_t index = (start + i) % workers_.size();
			if (workers_[index]->IsRunning()) {
				return index;
			}
		}
	}

	rejected_status = RedisSubmitStatus::kDisconnected;
	error = "no healthy redis workers";
	return static_cast<size_t>(-1);
}

bool RedisClientThreadGroup::Submit(RedisRequest& request,
									bool has_routing_key,
									const std::string& routing_key,
									RedisSubmitStatus& rejected_status,
									std::string& error) {
	rejected_status = RedisSubmitStatus::kAccepted;
	error.clear();

	if (!ReserveUnsentSlot()) {
		rejected_status = RedisSubmitStatus::kQueueFull;
		error = "redis request queue is full";
		counters_->rejected_requests.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	size_t worker_index = static_cast<size_t>(-1);
	if (request.preferred_worker_index && *request.preferred_worker_index < workers_.size()) {
		worker_index = *request.preferred_worker_index;
		auto& worker = workers_[worker_index];
		if (!worker->IsRunning() ||
			(!worker->IsHealthy() && !config_.reconnect.queue_while_disconnected)) {
			rejected_status = worker->IsRunning()
				? RedisSubmitStatus::kDisconnected
				: RedisSubmitStatus::kNotRunning;
			error = "preferred redis worker is not healthy";
			ReleaseUnsentSlot();
			counters_->rejected_requests.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
	} else {
		worker_index = ChooseWorker(has_routing_key, routing_key, rejected_status, error);
		if (worker_index == static_cast<size_t>(-1)) {
			ReleaseUnsentSlot();
			counters_->rejected_requests.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
	}

	if (!workers_[worker_index]->Enqueue(std::move(request))) {
		rejected_status = RedisSubmitStatus::kQueueFull;
		error = "redis worker request queue rejected enqueue";
		ReleaseUnsentSlot();
		counters_->rejected_requests.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	counters_->accepted_requests.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool RedisClientThreadGroup::IsRunning() const {
	return std::any_of(workers_.begin(), workers_.end(), [](const auto& worker) {
		return worker && worker->IsRunning();
	});
}

bool RedisClientThreadGroup::IsHealthy() const {
	return !workers_.empty() &&
		   std::all_of(workers_.begin(), workers_.end(), [](const auto& worker) {
			   return worker && worker->IsHealthy();
		   });
}

RedisClientStats RedisClientThreadGroup::GetStats(RedisClientState state) const {
	RedisClientStats stats;
	stats.state = state;
	stats.running = IsRunning();
	stats.healthy = IsHealthy();
	stats.worker_count = workers_.size();
	stats.inflight_requests = counters_->inflight_global.load(std::memory_order_acquire);
	stats.accepted_requests = counters_->accepted_requests.load(std::memory_order_relaxed);
	stats.rejected_requests = counters_->rejected_requests.load(std::memory_order_relaxed);
	stats.completed_requests = counters_->completed_requests.load(std::memory_order_relaxed);
	stats.timed_out_requests = counters_->timed_out_requests.load(std::memory_order_relaxed);
	stats.workers.reserve(workers_.size());
	for (const auto& worker : workers_) {
		if (!worker) continue;
		auto worker_stats = worker->GetStats();
		stats.queued_requests += worker_stats.queued_requests;
		stats.unsent_requests += worker_stats.unsent_requests;
		stats.workers.push_back(worker_stats);
	}
	return stats;
}

}  // namespace redis
}  // namespace engine
