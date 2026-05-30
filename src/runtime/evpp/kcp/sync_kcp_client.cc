#include "runtime/evpp/kcp/sync_kcp_client.h"

#include "runtime/evpp/gettimeofday.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/sockets.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

extern "C" {
#include "thirdparty/kcp/ikcp.h"
}

namespace evpp {
namespace kcp {
namespace sync {

// KCP‑compatible signed time difference (handles 32‑bit wrap‑around).
static inline IINT32 kcp_timediff(IUINT32 later, IUINT32 earlier) {
	return static_cast<IINT32>(later - earlier);
}

static inline IUINT32 kcp_clock() {
	return static_cast<IUINT32>(utcmicrosecond() / 1000);
}

// ---------------------------------------------------------------------------
// KCP output callback — invoked by KCP when it wants to emit a raw UDP packet.
// ---------------------------------------------------------------------------
static int kcp_output_callback(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
	Client* self = static_cast<Client*>(user);
	if (self->sockfd() == INVALID_SOCKET) {
		return -1;
	}
	int sent = ::send(self->sockfd(), buf, len, 0);
	return (sent == len) ? 0 : -1;
}

// Client
Client::Client() {
	memset(&remote_addr_, 0, sizeof(remote_addr_));
}

Client::~Client() {
	Close();
}

void Client::SetKcpNodelay(int nodelay, int interval, int resend, int nc) {
	kcp_nodelay_ = nodelay ? 1 : 0;
	kcp_interval_ = interval > 0 ? interval : 10;
	kcp_resend_ = resend >= 0 ? resend : 0;
	kcp_nc_ = nc ? 1 : 0;
}

void Client::SetKcpWndSize(int sndwnd, int rcvwnd) {
	if (sndwnd > 0) {
		kcp_sndwnd_ = sndwnd;
	}
	if (rcvwnd > 0) {
		kcp_rcvwnd_ = rcvwnd;
	}
}

void Client::SetKcpMtu(int mtu) {
	if (mtu > 24 && mtu <= 65507) {
		kcp_mtu_ = mtu;
	}
}

bool Client::Connect(const struct sockaddr_in& addr, uint32_t conv) {
	memset(&remote_addr_, 0, sizeof(remote_addr_));
	memcpy(&remote_addr_, &addr, sizeof(addr));
	conv_ = conv;
	return Connect();
}

bool Client::Connect(const char* host, int port, uint32_t conv) {
	std::string addr;
	if (strchr(host, ':')) {
		addr = std::string("[") + host + "]:" + std::to_string(port);
	} else {
		addr = std::string(host) + ":" + std::to_string(port);
	}
	return Connect(addr.c_str(), conv);
}

bool Client::Connect(const struct sockaddr_storage& addr, uint32_t conv) {
	memcpy(&remote_addr_, &addr, sizeof(remote_addr_));
	conv_ = conv;
	return Connect();
}

bool Client::Connect(const char* addr, uint32_t conv) {
	conv_ = conv;
	if (!sock::ParseFromIPPort(addr, remote_addr_)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCP client failed to parse address: {}", addr);
		return false;
	}
	return Connect();
}

bool Client::Connect() {
	Close();
	int domain = (remote_addr_.ss_family == AF_INET6) ? AF_INET6 : AF_INET;
	sockfd_ = ::socket(domain, SOCK_DGRAM, 0);
	if (sockfd_ == INVALID_SOCKET) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "KCP client failed to create socket, errno={} {}",
						 EVPP_ERRNO,
						 strerror(EVPP_ERRNO));
		return false;
	}
	sock::SetReuseAddr(sockfd_);

	struct sockaddr* addr = reinterpret_cast<struct sockaddr*>(&remote_addr_);
	socklen_t addrlen =
		(remote_addr_.ss_family == AF_INET6) ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
	int ret = ::connect(sockfd_, addr, addrlen);

	if (ret != 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "KCP client failed to connect to {}, errno={} {}",
						 sock::ToIPPort(&remote_addr_),
						 EVPP_ERRNO,
						 strerror(EVPP_ERRNO));
		Close();
		return false;
	}

	connected_ = true;
	if (!InitKcp()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCP client ikcp_create failed");
		Close();
		return false;
	}
	return true;
}

bool Client::InitKcp() {
	kcp_ = ikcp_create(conv_, this);
	if (!kcp_) {
		return false;
	}
	ikcp_setoutput(kcp_, kcp_output_callback);
	ikcp_wndsize(kcp_, kcp_sndwnd_, kcp_rcvwnd_);
	ikcp_setmtu(kcp_, kcp_mtu_);
	ikcp_nodelay(kcp_, kcp_nodelay_, kcp_interval_, kcp_resend_, kcp_nc_);
	return true;
}

void Client::Close() {
	if (kcp_) {
		ikcp_release(kcp_);
		kcp_ = nullptr;
	}
	if (sockfd_ != INVALID_SOCKET) {
		EVUTIL_CLOSESOCKET(sockfd_);
	}
	sockfd_ = INVALID_SOCKET;
	connected_ = false;
}

bool Client::Send(const std::string& msg) {
	return Send(msg.data(), msg.size());
}

bool Client::Send(const char* msg, size_t len) {
	if (!connected_ || !kcp_ || (!msg && len > 0) || len == 0 ||
		len > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		return false;
	}
	int ret = ikcp_send(kcp_, msg, static_cast<int>(len));
	if (ret < 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "KCP client ikcp_send failed ret={}", ret);
		return false;
	}
	// Flush so data goes out immediately.
	ikcp_update(kcp_, kcp_clock());
	return true;
}

std::string Client::DoRequest(const std::string& data, uint32_t timeout_ms) {
	if (!Send(data)) {
		int eno = EVPP_ERRNO;
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "KCP client send failed, errno={} {} dlen={}",
						 eno,
						 strerror(eno),
						 data.size());
		return "";
	}

	IUINT32 start = kcp_clock();
	char raw_buf[65536];
	sock::SetTimeout(sockfd_, timeout_ms == 0 ? 10 : (std::min)(timeout_ms, 10u));

	while (true) {
		IUINT32 now = kcp_clock();
		if (timeout_ms > 0 && kcp_timediff(now, start) >= static_cast<IINT32>(timeout_ms)) {
			ENGINE_LOG_ERROR(
				engine::GetLogger(), "KCP client DoRequest timeout after {}ms", timeout_ms);
			return "";
		}

		// Drain incoming UDP packets and feed them to KCP.
		for (;;) {
			int n = ::recv(sockfd_, raw_buf, sizeof(raw_buf), 0);
			if (n > 0) {
				int ret = ikcp_input(kcp_, raw_buf, n);
				if (ret < 0) {
					ENGINE_LOG_WARN(engine::GetLogger(), "KCP client ikcp_input failed ret={}", ret);
				}
			} else if (n == 0) {
				break;	// graceful shutdown on connected UDP socket
			} else {
				int eno = EVPP_ERRNO;
				if (EVUTIL_ERR_RW_RETRIABLE(eno)) {
					break;	// timeout or would-block, no more data for now
				}
				ENGINE_LOG_ERROR(
					engine::GetLogger(), "KCP client recv fatal errno={} {}", eno, strerror(eno));
				return "";	// fatal socket error
			}
		}

		// Drive the KCP state machine.
		ikcp_update(kcp_, now);

		// Check for a complete application‑level response first — if the
		// response arrived just before the connection died, return it.
		int peek_size = ikcp_peeksize(kcp_);
		if (peek_size > 0) {
			std::vector<char> kcp_buf(static_cast<size_t>(peek_size));
			int hr = ikcp_recv(kcp_, kcp_buf.data(), peek_size);
			if (hr > 0) {
				return std::string(kcp_buf.data(), static_cast<size_t>(hr));
			}
			if (hr < -1) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "KCP client DoRequest recv fatal hr={}", hr);
				return "";
			}
		}
		if (peek_size < -1) {
			ENGINE_LOG_ERROR(
				engine::GetLogger(), "KCP client DoRequest peek fatal size={}", peek_size);
			return "";
		}

		// Connection declared dead (dead_link exceeded).
		if (kcp_->state == static_cast<IUINT32>(-1)) {
			ENGINE_LOG_ERROR(engine::GetLogger(), "KCP client DoRequest connection dead");
			return "";
		}

		usleep(1000);  // 1 ms back‑off
	}
}

std::string Client::DoRequest(const std::string& remote_ip,
							  int port,
							  const std::string& data,
							  uint32_t timeout_ms,
							  uint32_t conv) {
	Client c;
	c.SetKcpConv(conv);
	if (!c.Connect(remote_ip.c_str(), port, conv)) {
		return "";
	}
	return c.DoRequest(data, timeout_ms);
}

}  // namespace sync
}  // namespace kcp
}  // namespace evpp
