#include "runtime/auth/session_manager.h"

#include <chrono>
#include <limits>
#include <random>
#include <sstream>

#include "runtime/auth/auth_backend.h"
#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace auth {

SessionManager& SessionManager::Instance() {
	static SessionManager instance;
	return instance;
}

void SessionManager::SetBackend(std::unique_ptr<AuthBackend> backend) {
	std::lock_guard<std::mutex> lock(mutex_);
	backend_ = std::shared_ptr<AuthBackend>(std::move(backend));
}

AuthBackend* SessionManager::GetBackend() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return backend_.get();
}

std::shared_ptr<AuthBackend> SessionManager::GetBackendSnapshot() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return backend_;
}

std::string SessionManager::GenerateSessionId() {
	static thread_local std::random_device rd;
	static thread_local std::mt19937 gen(rd());
	static thread_local std::uniform_int_distribution<uint64_t> dist;

	std::ostringstream ss;
	ss << std::hex << dist(gen) << dist(gen);
	return ss.str();
}

SessionInfo SessionManager::CreateSession(const std::string& entity_id,
										   evpp::TCPConnPtr conn) {
	ENGINE_PROFILE_AUTH_CREATE_SESSION();
	std::lock_guard<std::mutex> lock(mutex_);

	if (conn) {
		auto old = conn_to_session_.find(conn.get());
		if (old != conn_to_session_.end()) {
			auto ref = conn_refs_.find(conn.get());
			if (ref == conn_refs_.end() || ref->second.expired()) {
				conn_refs_.erase(conn.get());
				conn_to_session_.erase(old);
			} else {
				RemoveSessionLocked(old->second, true);
			}
		}
	}

	// Enforce max sessions per account
	if (max_sessions_ > 0) {
		size_t account_sessions = 0;
		for (const auto& [sid, info] : sessions_) {
			if (info.entity_id == entity_id) ++account_sessions;
		}

		while (account_sessions >= max_sessions_) {
			std::string oldest_sid;
			int64_t oldest_created = std::numeric_limits<int64_t>::max();
			for (const auto& [sid, info] : sessions_) {
				if (info.entity_id == entity_id && info.created_at < oldest_created) {
					oldest_sid = sid;
					oldest_created = info.created_at;
				}
			}
			if (oldest_sid.empty()) break;

			RemoveSessionLocked(oldest_sid, true);
			--account_sessions;
		}
	}

	std::string sid = GenerateSessionId();
	auto now = std::chrono::system_clock::now();
	auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
					  now.time_since_epoch()).count();

	SessionInfo info;
	info.session_id = sid;
	info.entity_id = entity_id;
	info.created_at = now_ts;
	info.expires_at = now_ts + 86400;  // 24h default
	info.auth_method = "token";

	sessions_[sid] = info;
	if (conn) {
		conn_to_session_[conn.get()] = sid;
		conn_refs_[conn.get()] = conn;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "SessionManager: created session [{}] for entity [{}]",
					sid, entity_id);

	return info;
}

bool SessionManager::IsSessionValid(const std::string& session_id) const {
	ENGINE_PROFILE_AUTH_VALIDATE();
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = sessions_.find(session_id);
	if (it == sessions_.end()) return false;

	auto now = std::chrono::system_clock::now();
	auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
					  now.time_since_epoch()).count();

	if (it->second.expires_at > 0 && now_ts > it->second.expires_at) {
		return false;
	}

	return true;
}

std::optional<SessionInfo> SessionManager::GetSession(evpp::TCPConnPtr conn) const {
	ENGINE_PROFILE_AUTH_GET_SESSION();
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = conn_to_session_.find(conn.get());
	if (it == conn_to_session_.end()) return std::nullopt;

	auto ref = conn_refs_.find(conn.get());
	if (ref == conn_refs_.end() || ref->second.expired()) return std::nullopt;

	auto sit = sessions_.find(it->second);
	if (sit == sessions_.end()) return std::nullopt;

	return sit->second;
}

std::optional<SessionInfo> SessionManager::GetSessionById(const std::string& session_id) const {
	ENGINE_PROFILE_AUTH_GET_SESSION();
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = sessions_.find(session_id);
	if (it == sessions_.end()) return std::nullopt;

	return it->second;
}

void SessionManager::RevokeSession(const std::string& session_id) {
	ENGINE_PROFILE_AUTH_REVOKE();
	std::lock_guard<std::mutex> lock(mutex_);

	RemoveSessionLocked(session_id, true);
}

void SessionManager::RevokeSession(evpp::TCPConnPtr conn) {
	std::string session_id;
	{
		std::lock_guard<std::mutex> lock(mutex_);

		auto it = conn_to_session_.find(conn.get());
		if (it == conn_to_session_.end()) return;

		auto ref = conn_refs_.find(conn.get());
		if (ref == conn_refs_.end() || ref->second.expired()) {
			conn_refs_.erase(conn.get());
			conn_to_session_.erase(it);
			return;
		}

		session_id = it->second;
	}

	RevokeSession(session_id);
}

void SessionManager::CleanupExpired() {
	ENGINE_PROFILE_AUTH_CLEANUP();
	std::lock_guard<std::mutex> lock(mutex_);

	auto now = std::chrono::system_clock::now();
	auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
					  now.time_since_epoch()).count();

	for (auto it = sessions_.begin(); it != sessions_.end(); ) {
		if (it->second.expires_at > 0 && now_ts > it->second.expires_at) {
			const std::string session_id = it->first;
			++it;
			RemoveSessionLocked(session_id, true);
		} else {
			++it;
		}
	}
}

void SessionManager::SetMaxSessionsPerAccount(size_t max) {
	std::lock_guard<std::mutex> lock(mutex_);
	max_sessions_ = max;
}

void SessionManager::RemoveSessionLocked(const std::string& session_id,
										 bool revoke_backend) {
	if (revoke_backend && backend_) {
		backend_->RevokeSession(session_id);
	}

	sessions_.erase(session_id);

	for (auto it = conn_to_session_.begin(); it != conn_to_session_.end(); ) {
		if (it->second == session_id) {
			conn_refs_.erase(it->first);
			it = conn_to_session_.erase(it);
		} else {
			++it;
		}
	}
}

}  // namespace auth
}  // namespace engine
