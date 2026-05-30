#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "runtime/core/engine_api.h"
#include "runtime/rpc/rpc_protocol.h"

namespace engine {
namespace rpc {

// RPC client for async and sync service calls.
// Uses msgid for request-response correlation.
//
// Transport integration:
//   RpcClient does NOT own network I/O. Instead, the transport layer
//   (e.g., a TCP connection from Lua) must:
//     1. Set a SendCallback via SetSendCallback() after creation.
//     2. Call OnResponse() when a response arrives from the wire.
//     3. Call ProcessTimeouts() periodically (done by UpdateRpcBindings).
//
//   Without a SendCallback, Call/CallAsync return an error immediately.
class CLOUD_ENGINE_API RpcClient {
public:
	// Called by the transport layer to serialise and transmit a request.
	// Receives the full RpcRequest — the transport decides how to encode
	// it (msgpack, JSON, length-prefixed binary, etc.).
	//
	// THREAD SAFETY: The callback is invoked synchronously from the
	// thread that calls Call/CallAsync/CallSync.  If these are called
	// from the Lua main thread, the callback runs on the main thread
	// and may safely access the Lua state.  Calling RpcClient from
	// other threads requires a thread-safe transport callback that
	// does NOT touch the Lua VM directly.
	using SendCallback = std::function<void(RpcRequest)>;

	RpcClient() = default;
	~RpcClient();

	RpcClient(const RpcClient&) = delete;
	RpcClient& operator=(const RpcClient&) = delete;

	// ── Transport integration ─────────────────────────────────────────

	// Set the callback used to send requests. Must be set before any
	// Call/CallAsync, otherwise they return an error immediately.
	void SetSendCallback(SendCallback cb);

	// ── Request methods ───────────────────────────────────────────────

	// Async call: enqueues a pending request, invokes the SendCallback,
	// and returns a future that resolves on response.
	// Returns a future with error if no SendCallback is set.
	std::future<RpcResponse> Call(const std::string& service,
								   const std::string& method,
								   const std::string& args_json);

	// Callback-based async call. timeout_ms controls when ProcessTimeouts()
	// resolves the callback with a timeout error.
	using ResponseCallback = std::function<void(const RpcResponse&)>;
	void CallAsync(const std::string& service,
				   const std::string& method,
				   const std::string& args_json,
				   ResponseCallback callback,
				   int timeout_ms = 5000);

	// Sync call: blocks until response or timeout.
	// WARNING: This blocks the calling thread. Only use from non-critical
	// threads — never from the main event loop.
	RpcResponse CallSync(const std::string& service,
						  const std::string& method,
						  const std::string& args_json,
						  int timeout_ms = 5000);

	// Handle an incoming response message (called by transport layer).
	void OnResponse(const RpcResponse& response);

	// Timeout handling. Call periodically (every frame) to clean up
	// expired requests. Fulfills promises/callbacks with error.
	void ProcessTimeouts();

	// ── Status ───────────────────────────────────────────────────────

	// Number of in-flight requests.
	size_t PendingCount() const;

	// Whether a send callback is set. Thread-safe.
	bool HasTransport() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return send_callback_ != nullptr;
	}

private:
	struct PendingRequest;

	uint32_t NextMsgIdLocked();
	void CompletePending(std::unique_ptr<PendingRequest> pending,
						 const RpcResponse& response) noexcept;
	void FailPending(uint32_t msgid, RpcResponse response) noexcept;

	// Common logic for Call/CallSync: enqueues a request and returns
	// a (msgid, future) pair.  If no transport is set the future is
	// already fulfilled with an error and no entry is added to pending_.
	std::pair<uint32_t, std::future<RpcResponse>> EnqueueRequest(
		const std::string& service,
		const std::string& method,
		const std::string& args_json,
		int timeout_ms);

	struct PendingRequest {
		std::promise<RpcResponse> promise;
		ResponseCallback callback;
		std::chrono::steady_clock::time_point deadline;
	};

	SendCallback send_callback_;
	std::atomic<uint32_t> next_msgid_{1};
	mutable std::mutex mutex_;
	std::unordered_map<uint32_t, std::unique_ptr<PendingRequest>> pending_;
};

}  // namespace rpc
}  // namespace engine
