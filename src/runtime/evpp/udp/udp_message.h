#pragma once

#include "runtime/evpp/buffer.h"
#include "runtime/evpp/sockets.h"
#include "runtime/evpp/sys_sockets.h"

namespace evpp {
namespace udp {
class EVPP_EXPORT Message : public Buffer {
	public:
	Message(evpp_socket_t fd, size_t buffer_size = 1472) : Buffer(buffer_size), sockfd_(fd) {
		memset(&remote_addr_, 0, sizeof(remote_addr_));
	}

	void set_remote_addr(const struct sockaddr& raddr);
	const struct sockaddr* remote_addr() const;
	struct sockaddr* mutable_remote_addr() {
		return sock::sockaddr_cast(&remote_addr_);
	}
	std::string remote_ip() const;

	evpp_socket_t sockfd() const {
		return sockfd_;
	}

	private:
	struct sockaddr_storage remote_addr_;
	int sockfd_;
};
typedef std::shared_ptr<Message> MessagePtr;

inline void Message::set_remote_addr(const struct sockaddr& raddr) {
	size_t len =
		(raddr.sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	memcpy(&remote_addr_, &raddr, len);
}

inline const struct sockaddr* Message::remote_addr() const {
	return sock::sockaddr_cast(&remote_addr_);
}

inline std::string Message::remote_ip() const {
	return sock::ToIP(remote_addr());
}

inline bool SendMessage(evpp_socket_t fd, const struct sockaddr* addr, const char* d, size_t dlen) {
	if (dlen == 0) {
		return true;
	}

	socklen_t addrlen =
		(addr->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	int sentn = ::sendto(fd, d, dlen, 0, addr, addrlen);
	if (sentn < 0 || static_cast<size_t>(sentn) != dlen) {
		return false;
	}

	return true;
}

inline bool SendMessage(evpp_socket_t fd, const struct sockaddr* addr, const std::string& d) {
	return SendMessage(fd, addr, d.data(), d.size());
}

inline bool SendMessage(evpp_socket_t fd, const struct sockaddr* addr, const Slice& d) {
	return SendMessage(fd, addr, d.data(), d.size());
}

inline bool SendMessage(const MessagePtr& msg) {
	return SendMessage(msg->sockfd(), msg->remote_addr(), msg->data(), msg->size());
}

}
}