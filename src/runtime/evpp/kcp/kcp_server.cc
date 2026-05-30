#include "runtime/evpp/kcp/kcp_server.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/event_loop_thread_pool.h"
#include "runtime/evpp/gettimeofday.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/utility.h"

extern "C" {
#include "thirdparty/kcp/ikcp.h"
}

namespace evpp {
namespace kcp {

// KCP header overhead in bytes (24-byte KCP header per protocol spec).
// Defined in ikcp.c as a global but not declared in ikcp.h, so we redeclare.
static constexpr int kKcpOverhead = 24;

// KCP‑compatible signed time difference (handles 32‑bit wrap‑around).
static inline IINT32 kcp_timediff(IUINT32 later, IUINT32 earlier) {
	return static_cast<IINT32>(later - earlier);
}

enum Status {
	kStarting = 0,
	kRunning = 1,
	kPaused = 2,
	kStopping = 3,
	kStopped = 4,
};

struct SessionKey {
	IUINT32 conv = 0;
	int family = AF_UNSPEC;
	uint16_t port = 0;
	uint32_t scope_id = 0;
	std::array<uint8_t, 16> addr{};

	bool operator==(const SessionKey& rhs) const {
		return conv == rhs.conv && family == rhs.family && port == rhs.port &&
			   scope_id == rhs.scope_id && addr == rhs.addr;
	}
};

struct SessionKeyHash {
	size_t operator()(const SessionKey& key) const {
		size_t h = 1469598103934665603ull;
		auto mix = [&h](uint64_t v) {
			for (int i = 0; i < 8; ++i) {
				h ^= static_cast<uint8_t>(v & 0xff);
				h *= 1099511628211ull;
				v >>= 8;
			}
		};
		mix(key.conv);
		mix(static_cast<uint64_t>(key.family));
		mix(key.port);
		mix(key.scope_id);
		for (uint8_t b : key.addr) {
			h ^= b;
			h *= 1099511628211ull;
		}
		return h;
	}
};

SessionKey MakeSessionKey(IUINT32 conv, const struct sockaddr_storage& addr) {
	SessionKey key;
	key.conv = conv;
	key.family = addr.ss_family;
	if (addr.ss_family == AF_INET) {
		const auto* sin = sock::sockaddr_in_cast(&addr);
		key.port = sin->sin_port;
		std::memcpy(key.addr.data(), &sin->sin_addr, sizeof(sin->sin_addr));
	} else if (addr.ss_family == AF_INET6) {
		const auto* sin6 = sock::sockaddr_in6_cast(&addr);
		key.port = sin6->sin6_port;
		key.scope_id = sin6->sin6_scope_id;
		std::memcpy(key.addr.data(), &sin6->sin6_addr, sizeof(sin6->sin6_addr));
	}
	return key;
}

struct OutgoingMessage {
	SessionKey key;
	std::string data;
};

// ---------------------------------------------------------------------------
// KCP clock — returns a 32‑bit millisecond counter.  KCP handles wrap‑around
// via _itimediff internally.
// ---------------------------------------------------------------------------
static inline IUINT32 kcp_clock() {
	return static_cast<IUINT32>(utcmicrosecond() / 1000);
}

// KcpSession — wraps a single ikcpcb instance pinned to one remote address.
class Server::KcpSession {
	public:
	KcpSession(const SessionKey& key,
			   IUINT32 conv,
			   const struct sockaddr_storage& remote_addr,
			   socklen_t addrlen,
			   evpp_socket_t fd,
			   int sndwnd,
			   int rcvwnd,
			   int mtu,
			   int nodelay,
			   int interval,
			   int resend,
			   int nc)
		: key_(key),
		  conv_(conv),
		  remote_addr_(remote_addr),
		  fd_(fd),
		  addrlen_(addrlen),
		  last_active_(kcp_clock()),
		  alive_(true) {
		kcp_ = ikcp_create(conv, this);
		if (!kcp_) {
			alive_ = false;
			return;
		}
		ikcp_setoutput(kcp_, &KcpSession::OutputCallback);
		ikcp_wndsize(kcp_, sndwnd, rcvwnd);
		ikcp_setmtu(kcp_, mtu);
		ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
		next_update_ = ikcp_check(kcp_, kcp_clock());
	}

	~KcpSession() {
		if (kcp_) {
			ikcp_release(kcp_);
			kcp_ = nullptr;
		}
	}

	IUINT32 conv() const {
		return conv_;
	}
	const struct sockaddr_storage& remote_addr() const {
		return remote_addr_;
	}
	const SessionKey& key() const {
		return key_;
	}
	bool alive() const {
		return alive_;
	}

	// Feed a raw UDP packet into KCP.
	int Input(const char* data, long size) {
		if (!kcp_) return -1;
		last_active_ = kcp_clock();
		return ikcp_input(kcp_, data, size);
	}

	// Try to read a reassembled application‑level message.
	// Returns the number of bytes written to buffer, or a negative value
	// (-1: no data, -2: internal error, -3: buffer too small).
	// Fatal errors (-2, -3) mark the session dead to prevent the recv
	// queue from growing unboundedly with unconsumed messages.
	int Recv(char* buffer, int len) {
		if (!kcp_) return -1;
		int n = ikcp_recv(kcp_, buffer, len);
		if (n > 0) {
			last_active_ = kcp_clock();
		} else if (n < -1) {
			alive_ = false;
		}
		return n;
	}

	// Peek the size of the next message in the recv queue.
	int PeekSize() const {
		if (!kcp_) return -1;
		return ikcp_peeksize(kcp_);
	}

	// Queue application data for sending.
	int Send(const char* buffer, int len) {
		if (!kcp_) return -1;
		last_active_ = kcp_clock();
		return ikcp_send(kcp_, buffer, len);
	}
	void MarkDead() {
		alive_ = false;
	}

	// Call ikcp_update if it is time to do so; returns the new deadline.
	IUINT32 Update(IUINT32 now) {
		if (!kcp_) return now + 100;  // IKCP default interval as fallback
		if (kcp_timediff(now, next_update_) >= 0) {
			ikcp_update(kcp_, now);
			next_update_ = ikcp_check(kcp_, now);
		}
		if (kcp_->state == static_cast<IUINT32>(-1)) {
			alive_ = false;
		}
		return next_update_;
	}

	// Check whether the session has been idle for too long.
	bool IsTimeout(uint32_t timeout_ms) const {
		if (timeout_ms == 0) {
			return false;
		}
		IUINT32 now = kcp_clock();
		return kcp_timediff(now, last_active_) > static_cast<IINT32>(timeout_ms);
	}

	// Called by KCP when it needs to emit a raw UDP packet.
	int OnOutput(const char* buf, int len) {
		struct sockaddr* addr = sock::sockaddr_cast(&remote_addr_);
		int sent = ::sendto(fd_, buf, len, 0, addr, addrlen_);
		return (sent == len) ? 0 : -1;
	}

	private:
	static int OutputCallback(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
		KcpSession* self = static_cast<KcpSession*>(user);
		return self->OnOutput(buf, len);
	}

	ikcpcb* kcp_ = nullptr;
	SessionKey key_;
	IUINT32 conv_;
	struct sockaddr_storage remote_addr_;
	evpp_socket_t fd_;
	socklen_t addrlen_ = 0;
	IUINT32 next_update_ = 0;
	IUINT32 last_active_ = 0;
	bool alive_ = true;
};

// RecvThread — owns the UDP socket, the session map, and the I/O loop.
class Server::RecvThread : public std::enable_shared_from_this<RecvThread> {
	public:
	explicit RecvThread(Server* srv)
		: fd_(INVALID_SOCKET), server_(srv), port_(-1), status_(kStopped) {
	}

	~RecvThread() {
		Stop();
		if (thread_ && thread_->joinable()) {
			try {
				thread_->join();
			} catch (const std::system_error& e) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "Caught a system_error:{}", e.what());
			}
		}
		CloseSocket();
	}

	bool Listen(int p) {
		port_ = p;
		evpp_socket_t fd = sock::CreateUDPServer(p);
		if (fd < 0) {
			ENGINE_LOG_ERROR(engine::GetLogger(), "kcp listen error on port {}", p);
			return false;
		}
		fd_.store(fd, std::memory_order_release);
		// Use a short timeout so the loop can drive ikcp_update regularly.
		sock::SetTimeout(fd, 10);
		return true;
	}

	bool Run() {
		status_.store(kStarting, std::memory_order_release);
		thread_.reset(CLOUDENGINE_MEM_NEW(std::thread, std::bind(&Server::RecvingLoop, server_, this)));
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [this]() {
			Status s = status_.load();
			return s != kStarting;
		});
		return status_.load() == kRunning;
	}

	void Stop() {
		Status s = status_.load(std::memory_order_acquire);
		if (s == kStopping || s == kStopped) {
			CloseSocket();
			cv_.notify_all();
			return;
		}
		status_.store(kStopping);
		CloseSocket();
		cv_.notify_all();
	}
	void WaitUntilStopped() {
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [this]() { return status_.load() == kStopped; });
	}
	void Pause() {
		if (!IsRunning()) return;
		status_.store(kPaused);
		cv_.notify_all();
	}
	void Continue() {
		if (!IsPaused()) return;
		status_.store(kRunning);
		cv_.notify_all();
	}

	bool IsRunning() const {
		return status_.load() == kRunning;
	}
	bool IsStopped() const {
		return status_.load() == kStopped;
	}
	bool IsPaused() const {
		return status_.load() == kPaused;
	}

	void SetStatus(Status s) {
		status_.store(s);
		cv_.notify_all();
	}

	evpp_socket_t fd() const {
		return fd_.load(std::memory_order_acquire);
	}
	int port() const {
		return port_;
	}
	Server* server() const {
		return server_;
	}
	bool QueueSend(const SessionKey& key, const char* data, size_t len) {
		if (!data && len > 0) return false;
		if (len == 0 || len > static_cast<size_t>((std::numeric_limits<int>::max)())) {
			return false;
		}
		Status s = status_.load(std::memory_order_acquire);
		if (s == kStopping || s == kStopped) {
			return false;
		}
		std::lock_guard<std::mutex> lock(outgoing_mutex_);
		outgoing_.push_back(OutgoingMessage{key, std::string(data, len)});
		return true;
	}
	std::vector<OutgoingMessage> TakeOutgoing() {
		std::vector<OutgoingMessage> out;
		std::lock_guard<std::mutex> lock(outgoing_mutex_);
		out.swap(outgoing_);
		return out;
	}

	// Wait on the condition variable while paused (returns false if interrupted).
	bool WaitWhilePaused() {
		std::unique_lock<std::mutex> lock(mutex_);
		if (status_.load() == kPaused) {
			cv_.wait(lock, [this]() { return status_.load() != kPaused; });
		}
		return status_.load() == kRunning;
	}

	// Session map (only accessed from the recv thread).
	std::unordered_map<SessionKey, std::shared_ptr<KcpSession>, SessionKeyHash> sessions_;

	private:
	void CloseSocket() {
		evpp_socket_t fd = fd_.exchange(INVALID_SOCKET, std::memory_order_acq_rel);
		if (fd != INVALID_SOCKET) {
			EVUTIL_CLOSESOCKET(fd);
		}
	}

	std::atomic<evpp_socket_t> fd_;
	Server* server_;
	int port_;
	std::shared_ptr<std::thread> thread_;
	std::atomic<Status> status_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::mutex outgoing_mutex_;
	std::vector<OutgoingMessage> outgoing_;
};

// Server
Server::Server() {
}
Server::~Server() {
	Stop(true);
}

bool Server::Init(int port) {
	if (port <= 0 || port > 65535) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCP listen port out of range: {}", port);
		return false;
	}
	RecvThreadPtr t(CLOUDENGINE_MEM_NEW(RecvThread, this));
	if (!t->Listen(port)) {
		return false;
	}
	recv_threads_.push_back(t);
	return true;
}

bool Server::Init(const std::vector<int>& ports) {
	if (ports.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCP listen ports must not be empty");
		return false;
	}
	std::vector<RecvThreadPtr> old_threads = recv_threads_;
	for (int p : ports) {
		if (!Init(p)) {
			for (auto& t : recv_threads_) {
				t->Stop();
				t->WaitUntilStopped();
			}
			recv_threads_ = old_threads;
			return false;
		}
	}
	return true;
}

bool Server::Init(const std::string& listen_ports) {
	std::vector<std::string> vec;
	StringSplit(listen_ports, ",", 0, vec);

	std::vector<int> v;
	for (auto& s : vec) {
		char* end = nullptr;
		errno = 0;
		long i = std::strtol(s.c_str(), &end, 10);
		if (errno != 0 || end == s.c_str() || *end != '\0' || i <= 0 || i > 65535) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "Cannot convert [{}] to an integer. 'listen_ports' format wrong.",
							 s);
			return false;
		}
		v.push_back(static_cast<int>(i));
	}
	return Init(v);
}

void Server::AfterFork() {
	// Nothing to do right now.
}

bool Server::Start() {
	if (recv_threads_.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCPServer has no listen ports");
		return false;
	}
	if (!message_handler_) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "MessageHandler DO NOT set!");
		return false;
	}

	for (auto& rt : recv_threads_) {
		if (!rt->Run()) {
			Stop(true);
			return false;
		}
	}
	return true;
}

void Server::Stop(bool wait_thread_exit) {
	for (auto& it : recv_threads_) {
		it->Stop();
	}

	if (wait_thread_exit) {
		for (auto& it : recv_threads_) {
			it->WaitUntilStopped();
		}
	}
}

void Server::Pause() {
	for (auto& it : recv_threads_) {
		if (it->IsRunning()) it->Pause();
	}
}

void Server::Continue() {
	for (auto& it : recv_threads_) {
		it->Continue();
	}
}

bool Server::IsRunning() const {
	if (recv_threads_.empty()) return false;
	for (auto& it : recv_threads_) {
		if (!it->IsRunning()) return false;
	}
	return true;
}

bool Server::IsStopped() const {
	if (recv_threads_.empty()) return true;
	for (auto& it : recv_threads_) {
		if (!it->IsStopped()) return false;
	}
	return true;
}

void Server::SetKcpNodelay(int nodelay, int interval, int resend, int nc) {
	kcp_nodelay_ = nodelay ? 1 : 0;
	kcp_interval_ = interval > 0 ? interval : 10;
	kcp_resend_ = resend >= 0 ? resend : 0;
	kcp_nc_ = nc ? 1 : 0;
}

void Server::SetKcpWndSize(int sndwnd, int rcvwnd) {
	if (sndwnd > 0) {
		kcp_sndwnd_ = sndwnd;
	}
	if (rcvwnd > 0) {
		kcp_rcvwnd_ = rcvwnd;
	}
}

void Server::SetKcpMtu(int mtu) {
	if (mtu > kKcpOverhead && mtu <= 65507) {
		kcp_mtu_ = mtu;
	}
}

void Server::SetSessionTimeoutMs(uint32_t timeout_ms) {
	session_timeout_ms_ = timeout_ms;
}

void Server::SetMaxMessageSize(size_t max_bytes) {
	max_message_size_ = max_bytes == 0 ? 1024 * 1024 : max_bytes;
}

void Server::DrainOutgoing(RecvThread* th, uint32_t now_ms) {
	IUINT32 now = static_cast<IUINT32>(now_ms);
	auto outgoing = th->TakeOutgoing();
	for (auto& item : outgoing) {
		auto it = th->sessions_.find(item.key);
		if (it == th->sessions_.end()) {
			continue;
		}
		auto& session = it->second;
		int ret = session->Send(item.data.data(), static_cast<int>(item.data.size()));
		if (ret < 0) {
			ENGINE_LOG_WARN(engine::GetLogger(),
							"KCP send failed conv={} remote={} ret={}",
							session->conv(),
							sock::ToIPPort(&session->remote_addr()),
							ret);
			session->MarkDead();
			continue;
		}
		session->Update(now);
	}
}

// ---------------------------------------------------------------------------
// RecvingLoop — the per‑port I/O thread.
// ---------------------------------------------------------------------------
void Server::RecvingLoop(RecvThread* th) {
	ENGINE_LOG_INFO(engine::GetLogger(), "KCPServer is running at 0.0.0.0:{}", th->port());

	th->SetStatus(kRunning);

	// Stack buffer for raw UDP reads.
	char raw_buf[65536];

	IUINT32 now = kcp_clock();
	IUINT32 last_cleanup = now;

	while (true) {
		if (th->IsPaused()) {
			if (!th->WaitWhilePaused()) {
				break;
			}
		}
		if (!th->IsRunning()) {
			break;
		}

		// --- Try to receive a raw UDP packet --------------------------------
		evpp_socket_t fd = th->fd();
		if (fd == INVALID_SOCKET) {
			break;
		}
		DrainOutgoing(th, now);

		struct sockaddr_storage from_addr = {};
		socklen_t addr_len = sizeof(from_addr);
		int readn = ::recvfrom(
			fd, raw_buf, sizeof(raw_buf), 0, sock::sockaddr_cast(&from_addr), &addr_len);

		if (readn >= kKcpOverhead) {
			IUINT32 conv = ikcp_getconv(raw_buf);
			SessionKey key = MakeSessionKey(conv, from_addr);

			// Look up or create the session for this conversation.
			KcpSessionPtr session;
			auto it = th->sessions_.find(key);
			if (it != th->sessions_.end()) {
				session = it->second;
			} else {
				session = std::make_shared<KcpSession>(key,
													   conv,
													   from_addr,
													   addr_len,
													   fd,
													   kcp_sndwnd_,
													   kcp_rcvwnd_,
													   kcp_mtu_,
													   kcp_nodelay_,
													   kcp_interval_,
													   kcp_resend_,
													   kcp_nc_);
				if (!session->alive()) {
					ENGINE_LOG_ERROR(engine::GetLogger(),
									 "KCP session init failed conv={} remote={}",
									 conv,
									 sock::ToIPPort(&from_addr));
					continue;
				}
				th->sessions_[key] = session;
				ENGINE_LOG_INFO(engine::GetLogger(),
								"KCP session created conv={} remote={}",
								conv,
								sock::ToIPPort(&from_addr));
			}

			// Feed the raw packet into KCP.
			int ret = session->Input(raw_buf, readn);
			if (ret < 0) {
				ENGINE_LOG_WARN(engine::GetLogger(), "ikcp_input failed conv={} ret={}", conv, ret);
			}

			// Drain any complete application messages from this session.
			for (;;) {
				int peek_size = session->PeekSize();
				if (peek_size < 0) break;  // no more complete messages
				if (static_cast<size_t>(peek_size) > max_message_size_) {
					ENGINE_LOG_WARN(engine::GetLogger(),
									"KCP message too large conv={} remote={} size={} max={}",
									conv,
									sock::ToIPPort(&from_addr),
									peek_size,
									max_message_size_);
					session->MarkDead();
					break;
				}
				std::vector<char> kcp_buf(static_cast<size_t>(peek_size));
				int n = session->Recv(kcp_buf.data(), peek_size);
				if (n < 0) break;

				MessagePtr msg(CLOUDENGINE_MEM_NEW(Message, session->conv(), n));
				msg->Write(kcp_buf.data(), n);
				msg->set_remote_addr(*sock::sockaddr_cast(&from_addr));
				std::weak_ptr<RecvThread> weak_thread = th->shared_from_this();
				SessionKey reply_key = session->key();
				msg->set_reply_callback([weak_thread, reply_key](const char* data, size_t len) {
					auto thread = weak_thread.lock();
					return thread ? thread->QueueSend(reply_key, data, len) : false;
				});

				if (tpool_) {
					EventLoop* loop = nullptr;
					if (IsRoundRobin()) {
						loop = tpool_->GetNextLoop();
					} else {
						uint64_t hash = conv;
						if (from_addr.ss_family == AF_INET) {
							hash = sock::sockaddr_in_cast(&from_addr)->sin_addr.s_addr;
						} else if (from_addr.ss_family == AF_INET6) {
							const auto* sin6 = sock::sockaddr_in6_cast(&from_addr);
							const auto* bytes = reinterpret_cast<const uint8_t*>(&sin6->sin6_addr);
							uint64_t lo = 0, hi = 0;
							memcpy(&lo, bytes, 8);
							memcpy(&hi, bytes + 8, 8);
							hash = lo ^ hi;
						}
						loop = tpool_->GetNextLoopWithHash(hash);
					}
					loop->RunInLoop(std::bind(message_handler_, loop, msg));
				} else {
					message_handler_(nullptr, msg);
				}
			}
			DrainOutgoing(th, kcp_clock());
		} else if (readn >= 0) {
			// Packet too small to be a valid KCP segment.
			// Ignore silently.
		} else {
			int eno = EVPP_ERRNO;
			if (!EVUTIL_ERR_RW_RETRIABLE(eno)) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "recvfrom errno={} {}", eno, strerror(eno));

				// don't add break
				//break;
			}
		}

		// --- Drive ikcp_update on all sessions ------------------------------
		now = kcp_clock();
		DrainOutgoing(th, now);
		for (auto& kv : th->sessions_) {
			kv.second->Update(now);
		}

		// --- Periodic cleanup of dead / idle sessions -----------------------
		if (kcp_timediff(now, last_cleanup) > 5000) {  // every ~5 seconds
			last_cleanup = now;
			auto it = th->sessions_.begin();
			while (it != th->sessions_.end()) {
				auto& session = it->second;
				if (!session->alive() || session->IsTimeout(session_timeout_ms_)) {
					ENGINE_LOG_INFO(engine::GetLogger(),
									"KCP session removed conv={} remote={} {}",
									session->conv(),
									sock::ToIPPort(&session->remote_addr()),
									session->alive() ? "timeout" : "dead");
					it = th->sessions_.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	ENGINE_LOG_INFO(engine::GetLogger(), "KCPServer port={} fd={} exited.", th->port(), th->fd());

	// Destroy all sessions cleanly.
	th->sessions_.clear();
	th->SetStatus(kStopped);
}

}  // namespace kcp
}  // namespace evpp
