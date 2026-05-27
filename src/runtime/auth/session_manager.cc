#include "runtime/auth/session_manager.h"

#include <chrono>
#include <random>
#include <sstream>

#include "runtime/auth/auth_backend.h"
#include "runtime/core/log/log.h"

namespace engine {
namespace auth {

SessionManager& SessionManager::Instance() {
	static SessionManager instance;
	return instance;
}

void SessionManager::SetBackend(std::unique_ptr<AuthBackend> backend) {
	backend_ = std::move(backend);
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
	std::lock_guard<std::mutex> lock(mutex_);

	// Enforce max sessions per account
	size_t account_sessions = 0;
	for (const auto& [sid, info] : sessions_) {
		if (info.entity_id == entity_id) ++account_sessions;
	}
	if (account_sessions >= max_sessions_) {
		// Revoke oldest session for this account
		for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
			if (it->second.entity_id == entity_id) {
				sessions_.erase(it);
				break;
			}
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
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "SessionManager: created session [{}] for entity [{}]",
					sid, entity_id);

	return info;
}

bool SessionManager::IsSessionValid(const std::string& session_id) const {
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = sessions_.find(session_id);
	if (it == sessions_.end()) return false;

	auto now = std::chrono::system_clock::now();
	auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
					  now.time_since_epoch()).count();

	if (it->second.expires_at > 0 && now_ts > it->second.expires_at) {
		return false;
	}

	if (backend_) {
		return backend_->ValidateSession(session_id);
	}

	return true;
}

std::optional<SessionInfo> SessionManager::GetSession(evpp::TCPConnPtr conn) const {
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = conn_to_session_.find(conn.get());
	if (it == conn_to_session_.end()) return std::nullopt;

	auto sit = sessions_.find(it->second);
	if (sit == sessions_.end()) return std::nullopt;

	return sit->second;
}

std::optional<SessionInfo> SessionManager::GetSessionById(const std::string& session_id) const {
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = sessions_.find(session_id);
	if (it == sessions_.end()) return std::nullopt;

	return it->second;
}

void SessionManager::RevokeSession(const std::string& session_id) {
	std::lock_guard<std::mutex> lock(mutex_);

	if (backend_) {
		backend_->RevokeSession(session_id);
	}
	sessions_.erase(session_id);

	// Clean up conn mapping
	for (auto it = conn_to_session_.begin(); it != conn_to_session_.end(); ++it) {
		if (it->second == session_id) {
			conn_to_session_.erase(it);
			break;
		}
	}
}

void SessionManager::RevokeSession(evpp::TCPConnPtr conn) {
	std::lock_guard<std::mutex> lock(mutex_);

	auto it = conn_to_session_.find(conn.get());
	if (it == conn_to_session_.end()) return;

	RevokeSession(it->second);
}

void SessionManager::CleanupExpired() {
	std::lock_guard<std::mutex> lock(mutex_);

	auto now = std::chrono::system_clock::now();
	auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
					  now.time_since_epoch()).count();

	for (auto it = sessions_.begin(); it != sessions_.end(); ) {
		if (it->second.expires_at > 0 && now_ts > it->second.expires_at) {
			if (backend_) {
				backend_->RevokeSession(it->first);
			}

			// Clean conn mapping
			for (auto cit = conn_to_session_.begin(); cit != conn_to_session_.end(); ++cit) {
				if (cit->second == it->first) {
					conn_to_session_.erase(cit);
					break;
				}
			}

			it = sessions_.erase(it);
		} else {
			++it;
		}
	}
}

}  // namespace auth
}  // namespace engine
