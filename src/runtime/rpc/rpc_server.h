#pragma once

#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"
#include "runtime/rpc/rpc_protocol.h"

namespace engine {
namespace rpc {

// Handler receives the request body and returns a response body.
// Return empty string for void methods.
// Thread-safety: handlers may be invoked from any thread (transport thread).
// Implementations MUST be thread-safe or defer work to the main thread.
using RpcServiceHandler = std::function<std::string(const std::string& method,
													 const std::string& body)>;

// Typed handler for a single method.
using RpcMethodHandler = std::function<std::string(const std::string& args_json)>;

// RPC server that dispatches incoming requests to registered service handlers.
//
// Thread safety: all public methods are protected by a shared_mutex.
// Register*/Unregister* take exclusive locks; HandleRequest/HasService
// take shared locks, allowing concurrent request processing.
class CLOUD_ENGINE_API RpcServer {
public:
	RpcServer() = default;
	~RpcServer() = default;

	RpcServer(const RpcServer&) = delete;
	RpcServer& operator=(const RpcServer&) = delete;

	// Register a full service handler (routes all methods for a service).
	void RegisterService(const std::string& name, RpcServiceHandler handler);

	// Register a single method handler for a service.
	void RegisterMethod(const std::string& service,
						const std::string& method,
						RpcMethodHandler handler);

	// Unregister a service.
	void UnregisterService(const std::string& name);

	// Handle an incoming request. Returns the response to send back.
	// Thread-safe — may be called from transport thread concurrently
	// with registration from the main thread.
	RpcResponse HandleRequest(const RpcRequest& request);

	// Check if a service is registered.
	bool HasService(const std::string& name) const;

	// Remove all registered services. Thread-safe.
	void Clear();

private:
	struct ServiceEntry {
		RpcServiceHandler service_handler;
		std::unordered_map<std::string, RpcMethodHandler> method_handlers;
	};

	mutable std::shared_mutex mutex_;
	std::unordered_map<std::string, ServiceEntry> services_;
};

}  // namespace rpc
}  // namespace engine
