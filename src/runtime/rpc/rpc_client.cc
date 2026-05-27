#include "runtime/rpc/rpc_client.h"

#include "runtime/core/log/log.h"

namespace engine {
namespace rpc {

std::future<RpcResponse> RpcClient::Call(const std::string& service,
										  const std::string& method,
										  const std::string& args_json) {
	uint32_t msgid = NextMsgId();

	auto pending = std::make_unique<PendingRequest>();
	auto future = pending->promise.get_future();
	pending->deadline = std::chrono::steady_clock::now() +
						std::chrono::milliseconds(5000);

	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_[msgid] = std::move(pending);
	}

	// Build request (caller is responsible for actual network send)
	RpcRequest req;
	req.header.msgid = msgid;
	req.header.service = service;
	req.header.method = method;
	req.header.type = RpcMessageType::kRequest;
	req.body = args_json;

	// Note: actual send is handled by the transport layer via the Lua binding.
	// The pending_ map stores the promise for correlation when response arrives.

	return future;
}

RpcResponse RpcClient::CallSync(const std::string& service,
								 const std::string& method,
								 const std::string& args_json,
								 int timeout_ms) {
	auto future = Call(service, method, args_json);
	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

	if (status == std::future_status::timeout) {
		RpcResponse resp;
		resp.success = false;
		resp.error_code = -1;
		resp.error_message = "timeout";
		return resp;
	}

	return future.get();
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

	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_[msgid] = std::move(pending);
	}
}

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
		pending->promise.set_value(response);
	}
}

void RpcClient::ProcessTimeouts() {
	auto now = std::chrono::steady_clock::now();
	std::vector<uint32_t> expired;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto& [msgid, pending] : pending_) {
			if (now >= pending->deadline) {
				expired.push_back(msgid);
			}
		}
	}

	for (auto msgid : expired) {
		std::unique_ptr<PendingRequest> pending;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = pending_.find(msgid);
			if (it == pending_.end()) continue;
			pending = std::move(it->second);
			pending_.erase(it);
		}

		RpcResponse resp;
		resp.msgid = msgid;
		resp.success = false;
		resp.error_code = -1;
		resp.error_message = "timeout";

		if (pending->callback) {
			pending->callback(resp);
		} else {
			pending->promise.set_value(resp);
		}
	}
}

}  // namespace rpc
}  // namespace engine
