#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/core/engine_api.h"

namespace evpp {
class EventLoop;
namespace http {
class Service;
}
}

namespace engine {
namespace monitoring {

// Built-in admin HTTP server exposing health, stats, and metrics endpoints.
// Runs on the engine's event loop, typically on a separate admin port.
class ENGINE_API AdminHttpServer {
public:
	AdminHttpServer();
	~AdminHttpServer();

	AdminHttpServer(const AdminHttpServer&) = delete;
	AdminHttpServer& operator=(const AdminHttpServer&) = delete;

	// Start listening on |port| at |bind_address|. Uses the given EventLoop.
	// Returns false if the port cannot be bound.
	bool Start(evpp::EventLoop* loop, int port,
			   const std::string& bind_address = "127.0.0.1");

	// Stop the server and release resources. Safe to call multiple times.
	void Stop();

	bool IsRunning() const { return running_; }
	int port() const { return port_; }
	const std::string& bind_address() const { return bind_address_; }

private:
	void RegisterHandlers();

	evpp::EventLoop* loop_ = nullptr;
	std::unique_ptr<evpp::http::Service> service_;
	int port_ = 0;
	std::string bind_address_ = "127.0.0.1";
	bool running_ = false;
};

}  // namespace monitoring
}  // namespace engine
