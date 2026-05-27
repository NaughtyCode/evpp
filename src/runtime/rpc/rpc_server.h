#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"
#include "runtime/rpc/rpc_protocol.h"

namespace engine {
namespace rpc {

// Handler receives the request body (JSON) and returns a response body (JSON).
// Return empty string for void methods.
using RpcServiceHandler = std::function<std::string(const std::string& method,
													 const std::string& body)>;

// Typed handler for a single method.
using RpcMethodHandler = std::function<std::string(const std::string& args_json)>;

// RPC server that dispatches incoming requests to registered service handlers.
class ENGINE_API RpcServer {
public:
	RpcServer() = default;
	~RpcServer() = default;

	// Register a full service handler (routes all methods for a service).
	void RegisterService(const std::string& name, RpcServiceHandler handler);

	// Register a single method handler for a service.
	void RegisterMethod(const std::string& service,
						const std::string& method,
						RpcMethodHandler handler);

	// Unregister a service.
	void UnregisterService(const std::string& name);

	// Handle an incoming request. Returns the response to send back.
	RpcResponse HandleRequest(const RpcRequest& request);

	// Check if a service is registered.
	bool HasService(const std::string& name) const;

private:
	struct ServiceEntry {
		RpcServiceHandler service_handler;
		std::unordered_map<std::string, RpcMethodHandler> method_handlers;
	};

	std::unordered_map<std::string, ServiceEntry> services_;
};

}  // namespace rpc
}  // namespace engine
