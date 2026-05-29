#include "runtime/evpp/udp/sync_udp_client.h"

#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/sockets.h"

namespace evpp {
namespace udp {
namespace sync {
namespace {

socklen_t SockAddrLen(const struct sockaddr_storage& addr) {
	return addr.ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
}

}

Client::Client() {
	sockfd_ = INVALID_SOCKET;
	memset(&remote_addr_, 0, sizeof(remote_addr_));
}

Client::~Client(void) {
	Close();
}

bool Client::Connect(const struct sockaddr_in& addr) {
	memset(&remote_addr_, 0, sizeof(remote_addr_));
	memcpy(&remote_addr_, &addr, sizeof(addr));
	return Connect();
}

bool Client::Connect(const char* host, int port) {
	std::string addr;
	if (strchr(host, ':')) {
		addr = std::string("[") + host + "]:" + std::to_string(port);
	} else {
		addr = std::string(host) + ":" + std::to_string(port);
	}
	return Connect(addr.c_str());
}

bool Client::Connect(const struct sockaddr_storage& addr) {
	memcpy(&remote_addr_, &addr, sizeof(remote_addr_));
	return Connect();
}

bool Client::Connect(const char* addr /*host:port*/) {
	if (!sock::ParseFromIPPort(addr, remote_addr_)) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "Failed to parse address: {}", addr);
		return false;
	}
	return Connect();
}

bool Client::Connect(const struct sockaddr& addr) {
	memset(&remote_addr_, 0, sizeof(remote_addr_));
	memcpy(&remote_addr_, &addr, sizeof(addr));
	return Connect();
}

bool Client::Connect() {
	Close();
	int domain = (remote_addr_.ss_family == AF_INET6) ? AF_INET6 : AF_INET;
	sockfd_ = ::socket(domain, SOCK_DGRAM, 0);
	if (sockfd_ == INVALID_SOCKET) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "Failed to create UDP socket, errno={} {}",
						 EVPP_ERRNO,
						 strerror(EVPP_ERRNO));
		return false;
	}
	sock::SetReuseAddr(sockfd_);

	struct sockaddr* addr = reinterpret_cast<struct sockaddr*>(&remote_addr_);
	socklen_t addrlen = SockAddrLen(remote_addr_);
	int ret = ::connect(sockfd_, addr, addrlen);

	if (ret != 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "Failed to connect to remote {}, errno={} {}",
						 sock::ToIPPort(&remote_addr_),
						 EVPP_ERRNO,
						 strerror(EVPP_ERRNO));
		Close();
		return false;
	}

	connected_ = true;
	return true;
}

void Client::Close() {
	if (sockfd_ != INVALID_SOCKET) {
		EVUTIL_CLOSESOCKET(sockfd_);
		sockfd_ = INVALID_SOCKET;
		connected_ = false;
	}
}


std::string Client::DoRequest(const std::string& data, uint32_t timeout_ms) {
	if (!Send(data)) {
		int eno = EVPP_ERRNO;
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "sent failed, errno={} {} , dlen={}",
						 eno,
						 strerror(eno),
						 data.size());
		return "";
	}

	sock::SetTimeout(sockfd_, timeout_ms);

	size_t buf_size = 65535;	 // The UDP max payload size
	MessagePtr msg(CLOUDENGINE_MEM_NEW(Message, sockfd_, buf_size));
	socklen_t addrLen = sizeof(struct sockaddr_storage);
	int readn =
		::recvfrom(sockfd_, msg->WriteBegin(), buf_size, 0, msg->mutable_remote_addr(), &addrLen);
	int err = EVPP_ERRNO;
	if (readn >= 0) {
		msg->WriteBytes(readn);
		return std::string(msg->data(), msg->size());
	} else {
		ENGINE_LOG_ERROR(engine::GetLogger(), "errno={} {} recvfrom return -1", err, strerror(err));
	}

	return "";
}

std::string Client::DoRequest(const std::string& remote_ip,
							  int port,
							  const std::string& udp_package_data,
							  uint32_t timeout_ms) {
	Client c;
	if (!c.Connect(remote_ip.data(), port)) {
		return "";
	}

	return c.DoRequest(udp_package_data, timeout_ms);
}

bool Client::Send(const char* msg, size_t len) {
	if (sockfd() == INVALID_SOCKET) {
		return false;
	}
	if (len > 65535) {
		return false;
	}
	if (connected_) {
		int sentn = ::send(sockfd(), msg, static_cast<int>(len), 0);
		return static_cast<size_t>(sentn) == len;
	}

	struct sockaddr* addr = reinterpret_cast<struct sockaddr*>(&remote_addr_);
	socklen_t addrlen = SockAddrLen(remote_addr_);
	int sentn = ::sendto(sockfd(), msg, static_cast<int>(len), 0, addr, addrlen);
	return sentn >= 0 && static_cast<size_t>(sentn) == len;
}

bool Client::Send(const std::string& msg) {
	return Send(msg.data(), msg.size());
}

bool Client::Send(const std::string& msg, const struct sockaddr_in& addr) {
	return Client::Send(msg.data(), msg.size(), addr);
}


bool Client::Send(const char* msg, size_t len, const struct sockaddr_in& addr) {
	Client c;
	if (!c.Connect(addr)) {
		return false;
	}

	return c.Send(msg, len);
}

bool Client::Send(const MessagePtr& msg) {
	return Client::Send(
		msg->data(), msg->size(), *reinterpret_cast<const struct sockaddr_in*>(msg->remote_addr()));
}

bool Client::Send(const Message* msg) {
	return Client::Send(
		msg->data(), msg->size(), *reinterpret_cast<const struct sockaddr_in*>(msg->remote_addr()));
}

}
}
}
