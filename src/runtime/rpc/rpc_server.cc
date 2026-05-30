#include "runtime/rpc/rpc_server.h"

#include <exception>
#include <mutex>

#include "runtime/core/log/log.h"

namespace engine {
namespace rpc {

void RpcServer::RegisterService(const std::string& name,
								 RpcServiceHandler handler) {
	std::lock_guard<std::shared_mutex> lock(mutex_);
	services_[name].service_handler = std::move(handler);

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "RpcServer: registered service [{}]", name);
}

void RpcServer::RegisterMethod(const std::string& service,
								const std::string& method,
								RpcMethodHandler handler) {
	std::lock_guard<std::shared_mutex> lock(mutex_);
	if (!handler) {
		auto it = services_.find(service);
		if (it != services_.end()) {
			it->second.method_handlers.erase(method);
		}
		return;
	}
	services_[service].method_handlers[method] = std::move(handler);
}

void RpcServer::UnregisterService(const std::string& name) {
	std::lock_guard<std::shared_mutex> lock(mutex_);
	services_.erase(name);
}

void RpcServer::Clear() {
	std::lock_guard<std::shared_mutex> lock(mutex_);
	services_.clear();
}

RpcResponse RpcServer::HandleRequest(const RpcRequest& request) {
	if (request.header.type != RpcMessageType::kRequest) {
		return RpcResponse::Error(request.header.msgid, 400,
			"invalid rpc request type");
	}
	if (request.header.service.empty()) {
		return RpcResponse::Error(request.header.msgid, 400,
			"service name is empty");
	}
	if (request.header.method.empty()) {
		return RpcResponse::Error(request.header.msgid, 400,
			"method name is empty");
	}

	// Take a snapshot of handlers under shared lock so the actual
	// invocation happens outside the lock (handlers may block).
	RpcServiceHandler service_handler;
	RpcMethodHandler method_handler;
	bool has_method = false;

	{
		std::shared_lock<std::shared_mutex> lock(mutex_);

		auto it = services_.find(request.header.service);
		if (it == services_.end()) {
			auto* logger = GetLogger();
			ENGINE_LOG_WARN(logger, "RpcServer: service not found [{}]",
							request.header.service);
			return RpcResponse::Error(request.header.msgid, 404,
				std::string("service not found: ") + request.header.service);
		}

		auto& entry = it->second;

		auto mit = entry.method_handlers.find(request.header.method);
		if (mit != entry.method_handlers.end() && mit->second) {
			method_handler = mit->second;
			has_method = true;
		} else if (entry.service_handler) {
			service_handler = entry.service_handler;
			has_method = true;
		}
	}

	// Invoke handler outside the lock — handler may block (e.g., the Lua
	// bind defer-to-main-thread pattern).  Captured std::function copies
	// keep the handler alive even if the service is unregistered mid-call.
	//
	// has_service is always true here (we returned early inside the lock
	// if the service was not found).  Only has_method can be false.
	if (!has_method) {
		return RpcResponse::Error(request.header.msgid, 405,
			std::string("method not found: ") + request.header.method);
	}

	try {
		if (method_handler) {
			return RpcResponse::Ok(request.header.msgid, method_handler(request.body));
		}
		return RpcResponse::Ok(request.header.msgid,
			service_handler(request.header.method, request.body));
	} catch (const std::exception& e) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "RpcServer: handler exception in [{}].[{}]: {}",
						 request.header.service, request.header.method, e.what());
		return RpcResponse::Error(request.header.msgid, 500, e.what());
	} catch (...) {
		auto* logger = GetLogger();
		ENGINE_LOG_ERROR(logger, "RpcServer: unknown exception in [{}].[{}]",
						 request.header.service, request.header.method);
		return RpcResponse::Error(request.header.msgid, 500, "unknown handler error");
	}
}

bool RpcServer::HasService(const std::string& name) const {
	std::shared_lock<std::shared_mutex> lock(mutex_);
	return services_.find(name) != services_.end();
}

}  // namespace rpc
}  // namespace engine
