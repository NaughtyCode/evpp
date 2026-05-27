#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"
#include "runtime/rpc/rpc_protocol.h"

namespace engine {
namespace rpc {

// RPC client for async and sync service calls.
// Uses msgid for request-response correlation.
class ENGINE_API RpcClient {
public:
	RpcClient() = default;
	~RpcClient() = default;

	// Async call: sends request, returns future that resolves on response.
	std::future<RpcResponse> Call(const std::string& service,
								   const std::string& method,
								   const std::string& args_json);

	// Sync call: blocks until response or timeout.
	RpcResponse CallSync(const std::string& service,
						  const std::string& method,
						  const std::string& args_json,
						  int timeout_ms = 5000);

	// Handle an incoming response message (called by transport layer).
	void OnResponse(const RpcResponse& response);

	// Callback-based async call.
	using ResponseCallback = std::function<void(const RpcResponse&)>;
	void CallAsync(const std::string& service,
				   const std::string& method,
				   const std::string& args_json,
				   ResponseCallback callback);

	// Timeout handling. Call periodically to clean up expired requests.
	void ProcessTimeouts();

private:
	uint32_t NextMsgId() { return next_msgid_.fetch_add(1, std::memory_order_relaxed); }

	struct PendingRequest {
		std::promise<RpcResponse> promise;
		ResponseCallback callback;
		std::chrono::steady_clock::time_point deadline;
	};

	std::atomic<uint32_t> next_msgid_{1};
	std::mutex mutex_;
	std::unordered_map<uint32_t, std::unique_ptr<PendingRequest>> pending_;
};

}  // namespace rpc
}  // namespace engine
