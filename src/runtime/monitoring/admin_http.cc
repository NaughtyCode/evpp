#include "runtime/monitoring/admin_http.h"

#include <chrono>
#include <sstream>

#include <runtime/evpp/event_loop.h>

#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/evpp/http/service.h"
#include "runtime/monitoring/metrics.h"

namespace engine {
namespace monitoring {

namespace {

// Engine uptime: time since first recorded in the uptime gauge.
// Set on first request; the gauge itself is bumped by the engine update loop.
std::chrono::steady_clock::time_point g_start_time;
std::once_flag g_start_time_init;

void EnsureStartTime() {
	std::call_once(g_start_time_init, []() {
		g_start_time = std::chrono::steady_clock::now();
	});
}

double UptimeSeconds() {
	EnsureStartTime();
	auto now = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(now - g_start_time).count();
}

void HandleHealth(evpp::EventLoop*, const evpp::http::ContextPtr&,
				  const evpp::http::HTTPSendResponseCallback& respcb) {
	EnsureStartTime();
	std::ostringstream oss;
	oss << "{\"status\":\"ok\",\"uptime_seconds\":" << UptimeSeconds()
		<< ",\"frame_count\":" << engine::Engine::Instance().frame_count()
		<< ",\"running\":" << (engine::Engine::Instance().running() ? "true" : "false")
		<< "}";
	respcb(oss.str());
}

void HandleStats(evpp::EventLoop*, const evpp::http::ContextPtr&,
				 const evpp::http::HTTPSendResponseCallback& respcb) {
	auto& metrics = MetricsRegistry::Instance();
	std::ostringstream oss;
	oss << "{\"uptime_seconds\":" << UptimeSeconds()
		<< ",\"frame_count\":" << engine::Engine::Instance().frame_count()
		<< ",\"connections_active\":" << metrics.connections_active().Value()
		<< ",\"connections_total\":" << metrics.connections_total().Value()
		<< ",\"messages_received\":" << metrics.messages_received_total().Value()
		<< ",\"messages_sent\":" << metrics.messages_sent_total().Value()
		<< ",\"timers_fired\":" << metrics.timers_fired_total().Value()
		<< ",\"db_requests\":" << metrics.db_requests_total().Value()
		<< ",\"db_dropped\":" << metrics.db_requests_dropped_total().Value()
		<< "}";
	respcb(oss.str());
}

void HandleMetrics(evpp::EventLoop*, const evpp::http::ContextPtr&,
				   const evpp::http::HTTPSendResponseCallback& respcb) {
	respcb(MetricsRegistry::Instance().ExportPrometheus());
}

}  // namespace

AdminHttpServer::AdminHttpServer() = default;

AdminHttpServer::~AdminHttpServer() {
	Stop();
}

bool AdminHttpServer::Start(evpp::EventLoop* loop, int port) {
	if (running_) return true;
	if (!loop) return false;

	loop_ = loop;
	port_ = port;

	service_ = std::make_unique<evpp::http::Service>(loop);
	if (!service_->Listen(port)) {
		auto* logger = engine::GetLogger();
		ENGINE_LOG_ERROR(logger, "AdminHttpServer: failed to listen on port {}", port);
		service_.reset();
		return false;
	}

	RegisterHandlers();
	running_ = true;

	auto* logger = engine::GetLogger();
	ENGINE_LOG_INFO(logger, "AdminHttpServer: listening on port {} (/health /stats /metrics)", port);
	return true;
}

void AdminHttpServer::Stop() {
	if (!running_) return;
	running_ = false;

	if (service_) {
		service_->Stop();
		service_.reset();
	}

	auto* logger = engine::GetLogger();
	ENGINE_LOG_INFO(logger, "AdminHttpServer: stopped");
}

void AdminHttpServer::RegisterHandlers() {
	service_->RegisterHandler("/health", HandleHealth);
	service_->RegisterHandler("/stats", HandleStats);
	service_->RegisterHandler("/metrics", HandleMetrics);
}

}  // namespace monitoring
}  // namespace engine
