#pragma once

#include <cstdint>

#include "runtime/evpp/inner_pre.h"

// Forward declaration — full definition comes from ikcp.h via the .cc file.
struct IKCPCB;
typedef struct IKCPCB ikcpcb;

namespace evpp {
namespace kcp {
namespace sync {

// Synchronous KCP client.  Not intended for production use — its primary
// purpose is testing the KCP server.  The client creates a UDP socket,
// wraps it in a KCP session, and provides blocking send / request‑response
// calls.
class CLOUD_ENGINE_API Client {
	public:
	Client();
	~Client();

	// Non-copyable, non-movable (owns socket and KCP control block).
	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;
	Client(Client&&) = delete;
	Client& operator=(Client&&) = delete;

	// Connect to a remote KCP server.  The conversation id must match
	// what the server expects (or the server can auto‑create sessions
	// based on conv).
	bool Connect(const char* host, int port, uint32_t conv = 0x11223344);
	bool Connect(const char* addr /*host:port*/, uint32_t conv = 0x11223344);
	bool Connect(const struct sockaddr_storage& addr, uint32_t conv = 0x11223344);
	bool Connect(const struct sockaddr_in& addr, uint32_t conv = 0x11223344);

	void Close();

	// Send raw application data (reliable, via KCP).
	bool Send(const std::string& msg);
	bool Send(const char* msg, size_t len);

	// Send a request and block until a response arrives or timeout expires.
	// @param[in]  data       Application payload.
	// @param[in]  timeout_ms Max wait time in milliseconds.
	// @return The response data, or empty string on failure / timeout.
	std::string DoRequest(const std::string& data, uint32_t timeout_ms);

	// One‑shot static helper.
	static std::string DoRequest(const std::string& remote_ip,
								 int port,
								 const std::string& data,
								 uint32_t timeout_ms,
								 uint32_t conv = 0x11223344);

	uint32_t conv() const {
		return conv_;
	}
	evpp_socket_t sockfd() const {
		return sockfd_;
	}

	// KCP tuning (must be called before Connect; see ikcp.h for details).
	void SetKcpNodelay(int nodelay, int interval, int resend, int nc);
	void SetKcpWndSize(int sndwnd, int rcvwnd);
	void SetKcpMtu(int mtu);
	void SetKcpConv(uint32_t conv) {
		conv_ = conv;
	}

	private:
	bool Connect();
	bool InitKcp();

	evpp_socket_t sockfd_ = INVALID_SOCKET;
	bool connected_ = false;
	struct sockaddr_storage remote_addr_;
	uint32_t conv_ = 0x11223344;

	// KCP control block (owned, released in Close).
	ikcpcb* kcp_ = nullptr;

	// KCP tuning parameters.
	int kcp_nodelay_ = 1;
	int kcp_interval_ = 10;
	int kcp_resend_ = 2;
	int kcp_nc_ = 1;
	int kcp_sndwnd_ = 128;
	int kcp_rcvwnd_ = 128;
	int kcp_mtu_ = 1400;
};

}  // namespace sync
}  // namespace kcp
}  // namespace evpp
