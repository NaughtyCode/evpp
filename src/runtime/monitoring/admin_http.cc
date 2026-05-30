#include "runtime/monitoring/admin_http.h"

#include <chrono>
#include <sstream>

#include <runtime/evpp/event_loop.h>

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/engine/engine.h"
#include "runtime/evpp/http/service.h"
#include "runtime/monitoring/metrics.h"
#include "runtime/physics/physics_engine_bridge.h"
#if defined(ENGINE_MONGODB_ENABLED)
#include "runtime/database/data_service/database_service.h"
#include "runtime/database/mongo/mongo_system.h"
#endif

namespace engine {
namespace monitoring {

namespace {

// Engine uptime: time since first recorded in the uptime gauge.
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

const char* CleanupPhaseToString(engine::Engine::CleanupPhase phase) {
	switch (phase) {
	case engine::Engine::CleanupPhase::NotStarted:      return "NotStarted";
	case engine::Engine::CleanupPhase::PhysicsShutdown:  return "PhysicsShutdown";
	case engine::Engine::CleanupPhase::DatabaseShutdown: return "DatabaseShutdown";
	case engine::Engine::CleanupPhase::NetworkShutdown:  return "NetworkShutdown";
	case engine::Engine::CleanupPhase::TimerShutdown:    return "TimerShutdown";
	case engine::Engine::CleanupPhase::ScriptDestroyed:  return "ScriptDestroyed";
	case engine::Engine::CleanupPhase::FinalLogs:        return "FinalLogs";
	case engine::Engine::CleanupPhase::Complete:         return "Complete";
	default:                                              return "Unknown";
	}
}

// Helper: emit JSON body and return the given HTTP status code.
void SendHealthJson(const evpp::http::ContextPtr& ctx,
					const evpp::http::HTTPSendResponseCallback& respcb,
					int http_code, const std::string& json) {
	ctx->set_response_http_code(http_code);
	respcb(json);
}

// ── /health/startup ──────────────────────────────────────────────────
// Returns 503 until Engine::Init() completes, then 200.

void HandleStartup(evpp::EventLoop*, const evpp::http::ContextPtr& ctx,
				   const evpp::http::HTTPSendResponseCallback& respcb) {
	auto& engine = engine::Engine::Instance();
	std::ostringstream oss;
	oss << "{\"status\":\"" << (engine.initialized() ? "started" : "starting") << "\"";
	auto phase = engine.cleanup_phase();
	if (phase != engine::Engine::CleanupPhase::NotStarted) {
		oss << ",\"cleanup_phase\":\"" << CleanupPhaseToString(phase) << "\"";
	}
	oss << "}";
	int code = engine.initialized() ? 200 : 503;
	SendHealthJson(ctx, respcb, code, oss.str());
}

// ── /health/readiness ────────────────────────────────────────────────
// Checks DB, physics, and MongoDB connectivity. Returns 200 only when
// all dependencies are healthy.

void HandleReadiness(evpp::EventLoop*, const evpp::http::ContextPtr& ctx,
					 const evpp::http::HTTPSendResponseCallback& respcb) {
	auto& engine = engine::Engine::Instance();

	// If we haven't finished Init yet, we can't be ready.
	if (!engine.initialized()) {
		SendHealthJson(ctx, respcb, 503,
			"{\"status\":\"not_ready\",\"checks\":{\"init\":\"pending\"}}");
		return;
	}

	// During cleanup, readiness is always false (draining).
	auto phase = engine.cleanup_phase();
	if (phase != engine::Engine::CleanupPhase::NotStarted) {
		std::ostringstream oss;
		oss << "{\"status\":\"draining\""
			<< ",\"cleanup_phase\":\"" << CleanupPhaseToString(phase) << "\""
			<< "}";
		SendHealthJson(ctx, respcb, 503, oss.str());
		return;
	}

	struct DepStatus {
		std::string status;  // "ok", "error", "degraded", "disabled"
		std::string message;
	};
	auto make_check = [&](DepStatus& d) -> std::string {
		std::ostringstream o;
		o << "{\"status\":\"" << d.status << "\",\"message\":\"" << d.message << "\"}";
		return o.str();
	};

	DepStatus db{"ok", ""};
	DepStatus mongodb{"ok", ""};
	DepStatus physics{"ok", ""};
	bool all_healthy = true;

	// DB health
#if defined(ENGINE_MONGODB_ENABLED)
	if (!DatabaseService::Instance().IsRunning()) {
		db.status = "error";
		db.message = "DatabaseService not running";
		all_healthy = false;
	} else if (!DatabaseService::Instance().IsHealthy()) {
		db.status = "degraded";
		db.message = "one or more DB threads unhealthy";
		all_healthy = false;
	}
#else
	db = {"disabled", "MongoDB not compiled"};
#endif

	// MongoDB config/driver health
#if defined(ENGINE_MONGODB_ENABLED)
	{
		auto& cfg = ConfigManager::Instance();
		bool mongo_loaded = cfg.IsMongoDbDevLoaded() || cfg.IsMongoDbPublicLoaded();
		bool mongo_init = mongo::MongoSystem::Instance().IsInitialized();
		if (!mongo_init) {
			mongodb.status = "error";
			mongodb.message = "MongoSystem not initialized";
			all_healthy = false;
		} else if (!mongo_loaded) {
			mongodb.status = "degraded";
			mongodb.message = "no MongoDB config loaded";
		}
	}
#else
	mongodb = {"disabled", "MongoDB not compiled"};
#endif

	// Physics health
	if (!PhysicsEngineBridge::Instance().IsInitialized()) {
		physics.status = "error";
		physics.message = "physics system not initialized";
		all_healthy = false;
	} else if (!PhysicsEngineBridge::Instance().IsHealthy()) {
		physics.status = "degraded";
		physics.message = "physics thread unhealthy";
		all_healthy = false;
	}

	std::ostringstream oss;
	oss << "{\"status\":\"" << (all_healthy ? "ready" : "not_ready") << "\""
		<< ",\"checks\":{"
		<< "\"db\":" << make_check(db)
		<< ",\"mongodb\":" << make_check(mongodb)
		<< ",\"physics\":" << make_check(physics)
		<< "}}";
	int code = all_healthy ? 200 : 503;
	SendHealthJson(ctx, respcb, code, oss.str());
}

// ── /health/liveness ─────────────────────────────────────────────────
// Lightweight check: event loop alive + basic sanity. Always fast.

void HandleLiveness(evpp::EventLoop*, const evpp::http::ContextPtr& ctx,
					const evpp::http::HTTPSendResponseCallback& respcb) {
	auto& engine = engine::Engine::Instance();
	std::ostringstream oss;
	bool alive = engine.running() || engine.initialized();

	oss << "{\"status\":\"" << (alive ? "alive" : "dead") << "\""
		<< ",\"frame_count\":" << engine.frame_count()
		<< ",\"uptime_seconds\":" << UptimeSeconds();

	auto phase = engine.cleanup_phase();
	if (phase != engine::Engine::CleanupPhase::NotStarted) {
		oss << ",\"cleanup_phase\":\"" << CleanupPhaseToString(phase) << "\"";
	}
	oss << "}";
	int code = alive ? 200 : 503;
	SendHealthJson(ctx, respcb, code, oss.str());
}

// ── /health/phase ────────────────────────────────────────────────────
// Exposes the current CleanupPhase for load-balancer drain coordination.

void HandleHealthPhase(evpp::EventLoop*, const evpp::http::ContextPtr& ctx,
					   const evpp::http::HTTPSendResponseCallback& respcb) {
	auto& engine = engine::Engine::Instance();
	auto phase = engine.cleanup_phase();
	std::ostringstream oss;
	oss << "{\"phase\":\"" << CleanupPhaseToString(phase) << "\""
		<< ",\"phase_index\":" << static_cast<int>(phase) << "}";
	SendHealthJson(ctx, respcb, 200, oss.str());
}

// ── /health (legacy) ─────────────────────────────────────────────────
// Backward-compatible: delegates to liveness semantics.

void HandleHealth(evpp::EventLoop* loop, const evpp::http::ContextPtr& ctx,
				  const evpp::http::HTTPSendResponseCallback& respcb) {
	HandleLiveness(loop, ctx, respcb);
}

// ── /stats ───────────────────────────────────────────────────────────

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

// ── /metrics ─────────────────────────────────────────────────────────

void HandleMetrics(evpp::EventLoop*, const evpp::http::ContextPtr&,
				   const evpp::http::HTTPSendResponseCallback& respcb) {
	respcb(MetricsRegistry::Instance().ExportPrometheus());
}

}  // namespace

AdminHttpServer::AdminHttpServer() = default;

AdminHttpServer::~AdminHttpServer() {
	Stop();
}

bool AdminHttpServer::Start(evpp::EventLoop* loop, int port,
							const std::string& bind_address) {
	if (running_) return true;
	if (!loop) return false;

	loop_ = loop;
	port_ = port;
	bind_address_ = bind_address;

	service_ = std::make_unique<evpp::http::Service>(loop);
	if (!service_->Listen(bind_address_, port)) {
		auto* logger = engine::GetLogger();
		ENGINE_LOG_ERROR(logger, "AdminHttpServer: failed to listen on {}:{}", bind_address_, port);
		service_->Stop();
		service_.reset();
		return false;
	}

	RegisterHandlers();
	running_ = true;

	auto* logger = engine::GetLogger();
	ENGINE_LOG_INFO(logger, "AdminHttpServer: listening on {}:{} "
					"(/health /health/startup /health/readiness /health/liveness /health/phase /stats /metrics)",
					bind_address_, port);
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
	service_->RegisterHandler("/health/startup", HandleStartup);
	service_->RegisterHandler("/health/readiness", HandleReadiness);
	service_->RegisterHandler("/health/liveness", HandleLiveness);
	service_->RegisterHandler("/health/phase", HandleHealthPhase);
	service_->RegisterHandler("/stats", HandleStats);
	service_->RegisterHandler("/metrics", HandleMetrics);
}

}  // namespace monitoring
}  // namespace engine
