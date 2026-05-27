#include "runtime/rpc/rpc_server.h"

#include "runtime/core/log/log.h"

namespace engine {
namespace rpc {

void RpcServer::RegisterService(const std::string& name,
								 RpcServiceHandler handler) {
	services_[name].service_handler = std::move(handler);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RpcServer: registered service [{}]", name);
}

void RpcServer::RegisterMethod(const std::string& service,
								const std::string& method,
								RpcMethodHandler handler) {
	services_[service].method_handlers[method] = std::move(handler);
}

void RpcServer::UnregisterService(const std::string& name) {
	services_.erase(name);
}

RpcResponse RpcServer::HandleRequest(const RpcRequest& request) {
	RpcResponse response;
	response.msgid = request.header.msgid;

	auto it = services_.find(request.header.service);
	if (it == services_.end()) {
		response.success = false;
		response.error_code = 404;
		response.error_message = "service not found: " + request.header.service;

		auto* logger = GetLogger();
		ENGINE_LOG_WARN(logger, "RpcServer: service not found [{}]",
						request.header.service);
		return response;
	}

	auto& entry = it->second;

	try {
		// Try method-specific handler first
		auto mit = entry.method_handlers.find(request.header.method);
		if (mit != entry.method_handlers.end()) {
			response.body = mit->second(request.body);
			response.success = true;
		} else if (entry.service_handler) {
			response.body = entry.service_handler(request.header.method, request.body);
			response.success = true;
		} else {
			response.success = false;
			response.error_code = 405;
			response.error_message = "method not found: " + request.header.method;
		}
	} catch (const std::exception& e) {
		response.success = false;
		response.error_code = 500;
		response.error_message = e.what();

		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "RpcServer: handler exception in [{}].[{}]: {}",
						 request.header.service, request.header.method, e.what());
	}

	return response;
}

bool RpcServer::HasService(const std::string& name) const {
	return services_.find(name) != services_.end();
}

}  // namespace rpc
}  // namespace engine
