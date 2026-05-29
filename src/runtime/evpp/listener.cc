#include "runtime/evpp/listener.h"

#include <chrono>
#include <thread>

#include "runtime/config/config.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/fd_channel.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/sockets.h"

namespace evpp {
Listener::Listener(EventLoop* l, const std::string& addr) : loop_(l), addr_(addr) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} addr={}", (void*) this, addr);
}

Listener::~Listener() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} fd={}", (void*) this, (chan_ ? chan_->fd() : INVALID_SOCKET));
	chan_.reset();
	if (fd_ >= 0) {
		EVUTIL_CLOSESOCKET(fd_);
		fd_ = INVALID_SOCKET;
	}
}

bool Listener::Listen(int backlog) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	fd_ = sock::CreateNonblockingSocket();
	if (fd_ < 0) {
		int serrno = EVPP_ERRNO;
		ENGINE_LOG_CRITICAL(
			engine::GetLogger(), "Create a nonblocking socket failed {}", strerror(serrno));
		return false;
	}

	struct sockaddr_storage addr = sock::ParseFromIPPort(addr_.data());
	sock::SetReuseAddr(fd_);
	int ret = -1;
	for (int attempt = 0; attempt < 5; ++attempt) {
		ret = ::bind(fd_, sock::sockaddr_cast(&addr), static_cast<socklen_t>(sizeof(addr)));
		if (ret == 0) break;
		if (attempt < 4) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
	if (ret < 0) {
		int serrno = EVPP_ERRNO;
		ENGINE_LOG_CRITICAL(
			engine::GetLogger(), "bind error :{} . addr={}", strerror(serrno), addr_);
		EVUTIL_CLOSESOCKET(fd_);
		fd_ = INVALID_SOCKET;
		return false;
	}

	ret = ::listen(fd_, backlog);
	if (ret < 0) {
		int serrno = EVPP_ERRNO;
		ENGINE_LOG_CRITICAL(engine::GetLogger(), "Listen failed {}", strerror(serrno));
		EVUTIL_CLOSESOCKET(fd_);
		fd_ = INVALID_SOCKET;
		return false;
	}
	return true;
}

void Listener::Accept() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	chan_.reset(CLOUDENGINE_MEM_NEW(FdChannel, loop_, fd_, true, false));
	chan_->SetReadCallback(std::bind(&Listener::HandleAccept, this));
	loop_->RunInLoop(std::bind(&FdChannel::AttachToLoop, chan_.get()));
	ENGINE_LOG_INFO(engine::GetLogger(), "TCPServer is running at {}", addr_);
}

void Listener::HandleAccept() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} A new connection is comming in", (void*) this);
	assert(loop_->IsInLoopThread());
	struct sockaddr_storage ss;
	socklen_t addrlen = sizeof(ss);
	int nfd = -1;
	if ((nfd = ::accept(fd_, sock::sockaddr_cast(&ss), &addrlen)) == -1) {
		int serrno = EVPP_ERRNO;
		if (serrno != EAGAIN && serrno != EINTR) {
			ENGINE_LOG_WARN(
				engine::GetLogger(), "{} bad accept {}", __FUNCTION__, strerror(serrno));
		}
		return;
	}

	if (evutil_make_socket_nonblocking(nfd) < 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "set fd={} nonblocking failed.", nfd);
		EVUTIL_CLOSESOCKET(nfd);
		return;
	}

	{
		auto sc = engine::ConfigManager::Instance().GetServerConfig();
		sock::SetKeepAlive(nfd, true,
		                   sc.tcp_keepalive.idle_sec,
		                   sc.tcp_keepalive.interval_sec,
		                   sc.tcp_keepalive.count);
	}

	std::string raddr = sock::ToIPPort(&ss);
	if (raddr.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "sock::ToIPPort(&ss) failed.");
		EVUTIL_CLOSESOCKET(nfd);
		return;
	}

	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} accepted a connection from {}, listen fd={}, client fd={}",
					 (void*) this,
					 raddr,
					 fd_,
					 nfd);

	if (new_conn_fn_) {
		new_conn_fn_(nfd, raddr, sock::sockaddr_in_cast(&ss));
	} else {
		ENGINE_LOG_WARN(engine::GetLogger(),
						"{} no NewConnectionCallback set, closing accepted fd={}",
						__FUNCTION__,
						nfd);
		EVUTIL_CLOSESOCKET(nfd);
	}
}

void Listener::Stop() {
	assert(loop_->IsInLoopThread());
	chan_->DisableAllEvent();
	// Don't call chan_->Close() here — it would delete event_ and set it
	// to nullptr, then ~FdChannel() calls Close() again which asserts on
	// the now-null event_. DisableAllEvent() already detaches the event
	// from the loop; the destructor handles the actual deletion.
}
}
