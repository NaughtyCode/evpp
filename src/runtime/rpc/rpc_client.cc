#include "runtime/rpc/rpc_client.h"

#include <exception>
#include <limits>
#include <vector>

#include "runtime/core/log/log.h"

namespace engine {
namespace rpc {

namespace {

constexpr int kLifecycleErrorCode = -1;
constexpr int kTransportErrorCode = -2;
constexpr int kRequestIdExhaustedCode = -4;

std::string ExceptionText(const char* prefix, const std::exception& e) {
	std::string message(prefix);
	message += ": ";
	message += e.what();
	return message;
}

void LogCallbackException(const char* context, const std::exception& e) noexcept {
	auto* logger = GetLogger();
	ENGINE_LOG_ERROR(logger, "RpcClient: callback exception in [{}]: {}",
					 context, e.what());
}

void LogCallbackException(const char* context) noexcept {
	auto* logger = GetLogger();
	ENGINE_LOG_ERROR(logger, "RpcClient: unknown callback exception in [{}]",
					 context);
}

}  // namespace

RpcClient::~RpcClient() {
	// Fulfill all pending requests with error on destruction.
	std::unordered_map<uint32_t, std::unique_ptr<PendingRequest>> remaining;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		remaining.swap(pending_);
	}
	for (auto& [msgid, pending] : remaining) {
		CompletePending(std::move(pending),
						RpcResponse::Error(msgid, kLifecycleErrorCode,
										   "client destroyed"));
	}
}

// ── Transport integration ───────────────────────────────────────────────

void RpcClient::SetSendCallback(SendCallback cb) {
	std::lock_guard<std::mutex> lock(mutex_);
	send_callback_ = std::move(cb);
}

// ── Request helpers ─────────────────────────────────────────────────────

uint32_t RpcClient::NextMsgIdLocked() {
	uint32_t id = next_msgid_.load(std::memory_order_relaxed);
	if (id == 0) id = 1;

	constexpr uint32_t kMaxId = std::numeric_limits<uint32_t>::max();
	for (uint32_t attempts = 0; attempts < kMaxId; ++attempts) {
		if (id != 0 && pending_.find(id) == pending_.end()) {
			next_msgid_.store(id == kMaxId ? 1 : id + 1,
							  std::memory_order_relaxed);
			return id;
		}
		id = (id == kMaxId) ? 1 : id + 1;
	}

	return 0;
}

void RpcClient::CompletePending(std::unique_ptr<PendingRequest> pending,
								const RpcResponse& response) noexcept {
	if (!pending) return;

	if (pending->callback) {
		try {
			pending->callback(response);
		} catch (const std::exception& e) {
			LogCallbackException("response", e);
		} catch (...) {
			LogCallbackException("response");
		}
		return;
	}

	try {
		pending->promise.set_value(response);
	} catch (...) {
		// Promise may already be satisfied by a racing timeout/response path.
	}
}

void RpcClient::FailPending(uint32_t msgid, RpcResponse response) noexcept {
	std::unique_ptr<PendingRequest> pending;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = pending_.find(msgid);
		if (it == pending_.end()) return;
		pending = std::move(it->second);
		pending_.erase(it);
	}

	CompletePending(std::move(pending), response);
}

std::pair<uint32_t, std::future<RpcResponse>> RpcClient::EnqueueRequest(
		const std::string& service,
		const std::string& method,
		const std::string& args_json,
		int timeout_ms) {
	auto pending = std::make_unique<PendingRequest>();
	auto future = pending->promise.get_future();
	pending->deadline = std::chrono::steady_clock::now() +
						std::chrono::milliseconds(timeout_ms);

	SendCallback sender;
	uint32_t msgid = 0;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		msgid = NextMsgIdLocked();
		if (msgid == 0) {
			pending->promise.set_value(RpcResponse::Error(
				0, kRequestIdExhaustedCode, "request id space exhausted"));
			return {0, std::move(future)};
		}

		if (!send_callback_) {
			pending->promise.set_value(
				RpcResponse::Error(msgid, kTransportErrorCode,
								   "no transport (call SetSendCallback first)"));
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

	try {
		sender(std::move(req));
	} catch (const std::exception& e) {
		FailPending(msgid, RpcResponse::Error(
			msgid, kTransportErrorCode,
			ExceptionText("transport send failed", e)));
	} catch (...) {
		FailPending(msgid, RpcResponse::Error(
			msgid, kTransportErrorCode, "transport send failed"));
	}
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
						   ResponseCallback callback,
						   int timeout_ms) {
	auto pending = std::make_unique<PendingRequest>();
	pending->callback = std::move(callback);
	pending->deadline = std::chrono::steady_clock::now() +
						std::chrono::milliseconds(timeout_ms);

	SendCallback sender;
	uint32_t msgid = 0;
	bool complete_locally = false;
	RpcResponse local_response;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		msgid = NextMsgIdLocked();
		if (msgid == 0) {
			complete_locally = true;
			local_response = RpcResponse::Error(
				0, kRequestIdExhaustedCode, "request id space exhausted");
		} else if (!send_callback_) {
			complete_locally = true;
			local_response = RpcResponse::Error(
				msgid, kTransportErrorCode,
				"no transport (call SetSendCallback first)");
		} else {
			sender = send_callback_;
			pending_[msgid] = std::move(pending);
		}
	}

	if (complete_locally) {
		CompletePending(std::move(pending), local_response);
		return;
	}

	RpcRequest req;
	req.header.msgid = msgid;
	req.header.service = service;
	req.header.method = method;
	req.header.type = RpcMessageType::kRequest;
	req.body = args_json;

	try {
		sender(std::move(req));
	} catch (const std::exception& e) {
		FailPending(msgid, RpcResponse::Error(
			msgid, kTransportErrorCode,
			ExceptionText("transport send failed", e)));
	} catch (...) {
		FailPending(msgid, RpcResponse::Error(
			msgid, kTransportErrorCode, "transport send failed"));
	}
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

	CompletePending(std::move(pending), response);
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
		CompletePending(std::move(pending),
						RpcResponse::Error(msgid, kLifecycleErrorCode,
										   "timeout"));
	}
}

size_t RpcClient::PendingCount() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return pending_.size();
}

}  // namespace rpc
}  // namespace engine
