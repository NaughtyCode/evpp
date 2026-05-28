#include "runtime/rpc/rpc_client.h"

#include "runtime/core/log/log.h"

namespace engine {
namespace rpc {

RpcClient::~RpcClient() {
	// Fulfill all pending requests with error on destruction.
	std::unordered_map<uint32_t, std::unique_ptr<PendingRequest>> remaining;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		remaining.swap(pending_);
	}
	for (auto& [msgid, pending] : remaining) {
		RpcResponse resp = RpcResponse::Error(msgid, -1, "client destroyed");
		if (pending->callback) {
			pending->callback(resp);
		} else {
			try { pending->promise.set_value(resp); } catch (...) {}
		}
	}
}

// ── Transport integration ───────────────────────────────────────────────

void RpcClient::SetSendCallback(SendCallback cb) {
	std::lock_guard<std::mutex> lock(mutex_);
	send_callback_ = std::move(cb);
}

// ── Request helpers ─────────────────────────────────────────────────────

uint32_t RpcClient::NextMsgId() {
	uint32_t id = next_msgid_.fetch_add(1, std::memory_order_relaxed);
	// Guard against zero (invalid) and overflow wrap.  At 1M req/s this
	// would take ~71 minutes; at realistic rates wrap is not a concern
	// but we guard regardless.
	if (id == 0) id = next_msgid_.fetch_add(1, std::memory_order_relaxed);
	return id;
}

std::pair<uint32_t, std::future<RpcResponse>> RpcClient::EnqueueRequest(
		const std::string& service,
		const std::string& method,
		const std::string& args_json,
		int timeout_ms) {
	uint32_t msgid = NextMsgId();

	auto pending = std::make_unique<PendingRequest>();
	auto future = pending->promise.get_future();
	pending->deadline = std::chrono::steady_clock::now() +
						std::chrono::milliseconds(timeout_ms);

	SendCallback sender;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!send_callback_) {
			pending->promise.set_value(
				RpcResponse::Error(msgid, -2, "no transport (call SetSendCallback first)"));
			return {msgid, std::move(future)};
		}
		sender = send_callback_;
		pending_[msgid] = std::move(pending);
	}

	RpcRequest req;
	req.header.msgid = msgid;
	req.header.service = service;
	req.header.method = method;
	req.header.type = RpcMessageType::kRequest;
	req.body = args_json;

	sender(std::move(req));
	return {msgid, std::move(future)};
}

// ── Request methods ─────────────────────────────────────────────────────

std::future<RpcResponse> RpcClient::Call(const std::string& service,
										  const std::string& method,
										  const std::string& args_json) {
	auto result = EnqueueRequest(service, method, args_json, 5000);
	return std::move(result.second);
}

void RpcClient::CallAsync(const std::string& service,
						   const std::string& method,
						   const std::string& args_json,
						   ResponseCallback callback) {
	uint32_t msgid = NextMsgId();

	auto pending = std::make_unique<PendingRequest>();
	pending->callback = std::move(callback);
	pending->deadline = std::chrono::steady_clock::now() +
						std::chrono::milliseconds(5000);

	SendCallback sender;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!send_callback_) {
			if (pending->callback) {
				pending->callback(RpcResponse::Error(
					msgid, -2, "no transport (call SetSendCallback first)"));
			}
			return;
		}
		sender = send_callback_;
		pending_[msgid] = std::move(pending);
	}

	RpcRequest req;
	req.header.msgid = msgid;
	req.header.service = service;
	req.header.method = method;
	req.header.type = RpcMessageType::kRequest;
	req.body = args_json;

	sender(std::move(req));
}

RpcResponse RpcClient::CallSync(const std::string& service,
								 const std::string& method,
								 const std::string& args_json,
								 int timeout_ms) {
	auto result = EnqueueRequest(service, method, args_json, timeout_ms);
	auto msgid = result.first;
	auto future = std::move(result.second);

	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

	if (status == std::future_status::timeout) {
		// Clean up the pending entry so it doesn't outlive the wait.
		{
			std::lock_guard<std::mutex> lock(mutex_);
			pending_.erase(msgid);
		}
		return RpcResponse::Error(msgid, -1, "timeout");
	}

	return future.get();
}

// ── Response handling ───────────────────────────────────────────────────

void RpcClient::OnResponse(const RpcResponse& response) {
	std::unique_ptr<PendingRequest> pending;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = pending_.find(response.msgid);
		if (it == pending_.end()) {
			auto* logger = GetLogger();
			ENGINE_LOG_WARN(logger, "RpcClient: unexpected response msgid=[{}]",
							response.msgid);
			return;
		}
		pending = std::move(it->second);
		pending_.erase(it);
	}

	if (pending->callback) {
		pending->callback(response);
	} else {
		try { pending->promise.set_value(response); } catch (...) {}
	}
}

// ── Timeout handling ────────────────────────────────────────────────────

void RpcClient::ProcessTimeouts() {
	auto now = std::chrono::steady_clock::now();
	std::vector<std::pair<uint32_t, std::unique_ptr<PendingRequest>>> expired;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto it = pending_.begin(); it != pending_.end(); ) {
			if (now >= it->second->deadline) {
				expired.emplace_back(it->first, std::move(it->second));
				it = pending_.erase(it);
			} else {
				++it;
			}
		}
	}

	for (auto& [msgid, pending] : expired) {
		RpcResponse resp = RpcResponse::Error(msgid, -1, "timeout");
		if (pending->callback) {
			pending->callback(resp);
		} else {
			try { pending->promise.set_value(resp); } catch (...) {}
		}
	}
}

size_t RpcClient::PendingCount() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return pending_.size();
}

}  // namespace rpc
}  // namespace engine
