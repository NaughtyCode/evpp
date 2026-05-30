#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "runtime/evpp/buffer.h"
#include "runtime/evpp/sockets.h"
#include "runtime/evpp/sys_sockets.h"

namespace evpp {
namespace kcp {

class CLOUD_ENGINE_API Message : public Buffer {
	public:
	Message(uint32_t conv, size_t buffer_size = 1472) : Buffer(buffer_size), conv_(conv) {
		memset(&remote_addr_, 0, sizeof(remote_addr_));
	}

	void set_remote_addr(const struct sockaddr& raddr);
	const struct sockaddr* remote_addr() const;
	struct sockaddr* mutable_remote_addr() {
		return sock::sockaddr_cast(&remote_addr_);
	}
	std::string remote_ip() const;

	uint32_t conv() const {
		return conv_;
	}
	void set_conv(uint32_t c) {
		conv_ = c;
	}

	void set_reply_callback(std::function<bool(const char*, size_t)> cb) {
		reply_fn_ = std::move(cb);
	}
	bool Reply(const char* data, size_t len) {
		return reply_fn_ ? reply_fn_(data, len) : false;
	}
	bool Reply(const std::string& data) {
		return Reply(data.data(), data.size());
	}

	private:
	struct sockaddr_storage remote_addr_;
	uint32_t conv_;
	std::function<bool(const char*, size_t)> reply_fn_;
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

}  // namespace kcp
}  // namespace evpp
