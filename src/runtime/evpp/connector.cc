#include "runtime/evpp/connector.h"

#include "runtime/evpp/dns_resolver.h"
#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/fd_channel.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/sockets.h"
#include "runtime/evpp/tcp_client.h"

namespace evpp {
Connector::Connector(EventLoop* l, TCPClient* client)
	: status_(kDisconnected),
	  loop_(l),
	  owner_tcp_client_(client),
	  remote_addr_(client->remote_addr()),
	  timeout_(client->connecting_timeout()) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} raddr={}", (void*) this, remote_addr_);
	if (sock::SplitHostPort(remote_addr_.data(), remote_host_, remote_port_)) {
		raddr_ = sock::ParseFromIPPort(remote_addr_.data());
	}
}

Connector::~Connector() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(loop_->IsInLoopThread());
	if (reconnect_timer_) {
		reconnect_timer_->Cancel();
		reconnect_timer_.reset();
	}
	if (status_ == kDisconnected || status_ == kDNSResolving) {
		assert(!chan_.get());
		if (status_ == kDNSResolving) {
			assert(!dns_resolver_.get());
			assert(!timer_.get());
		}
	} else if (!IsConnected()) {
		// A connected tcp-connection's sockfd has been transfered to TCPConn.
		// But the sockfd of unconnected tcp-connections need to be closed by myself.
		ENGINE_LOG_TRACE(engine::GetLogger(), "this={} close({})", (void*) this, chan_->fd());
		assert(own_fd_);
		assert(chan_->fd() == fd_);
		EVUTIL_CLOSESOCKET(fd_);
		fd_ = INVALID_SOCKET;
	}

	assert(fd_ < 0);
	chan_.reset();
}

void Connector::Start() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} Try to connect {} status={}",
					 (void*) this,
					 remote_addr_,
					 StatusToString());
	assert(loop_->IsInLoopThread());

	timer_.reset(new TimerEventWatcher(
		loop_, std::bind(&Connector::OnConnectTimeout, shared_from_this()), timeout_));
	timer_->Init();
	timer_->AsyncWait();

	if (!sock::IsZeroAddress(&raddr_)) {
		Connect();
		return;
	}

	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} The remote address {} is a host, try to resolve its IP address.",
					 (void*) this,
					 remote_addr_);
	status_ = kDNSResolving;
	auto f = std::bind(&Connector::OnDNSResolved, shared_from_this(), std::placeholders::_1);
	dns_resolver_ = std::make_shared<DNSResolver>(loop_, remote_host_, timeout_, f);
	dns_resolver_->Start();
}


void Connector::Cancel() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} Cancel to connect {} status={}",
					 (void*) this,
					 remote_addr_,
					 StatusToString());
	assert(loop_->IsInLoopThread());
	if (dns_resolver_) {
		dns_resolver_->Cancel();
		dns_resolver_.reset();
	}

	if (timer_) {
		timer_->Cancel();
		timer_.reset();
	}

	if (reconnect_timer_) {
		reconnect_timer_->Cancel();
		reconnect_timer_.reset();
	}

	if (status_ == kDNSResolving) {
		assert(chan_.get() == nullptr);
		conn_fn_(-1, "");
	}

	if (chan_.get()) {
		assert(status_ != kDNSResolving);
		chan_->DisableAllEvent();
		chan_->Close();
	}
}

void Connector::Connect() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} {} status={}", (void*) this, remote_addr_, StatusToString());
	assert(fd_ == INVALID_SOCKET);
	fd_ = sock::CreateNonblockingSocket();
	if (fd_ < 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "CreateNonblockingSocket failed errno={} {}",
						 EVPP_ERRNO,
						 strerror(EVPP_ERRNO));
		HandleError();
		return;
	}
	own_fd_ = true;
	const std::string& laddr = owner_tcp_client_->local_addr();
	if (!laddr.empty()) {
		struct sockaddr_storage ss = sock::ParseFromIPPort(laddr.data());
		struct sockaddr* addr = sock::sockaddr_cast(&ss);
		int rc = ::bind(fd_, addr, sizeof(ss));
		if (rc != 0) {
			int serrno = EVPP_ERRNO;
			ENGINE_LOG_ERROR(
				engine::GetLogger(), "bind failed, errno={} {}", serrno, strerror(serrno));
			HandleError();
			return;
		}
	}
	struct sockaddr* addr = sock::sockaddr_cast(&raddr_);
	int rc = ::connect(fd_, addr, sizeof(raddr_));
	if (rc != 0) {
		int serrno = EVPP_ERRNO;
		if (!EVUTIL_ERR_CONNECT_RETRIABLE(serrno)) {
			HandleError();
			return;
		} else {
			// TODO how to do it
		}
	}

	status_ = kConnecting;

	chan_.reset(new FdChannel(loop_, fd_, false, true));
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} new FdChannel p={} fd={}",
					 (void*) this,
					 (void*) chan_.get(),
					 chan_->fd());
	chan_->SetWriteCallback(std::bind(&Connector::HandleWrite, shared_from_this()));
	chan_->AttachToLoop();
}

void Connector::HandleWrite() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} {} status={}", (void*) this, remote_addr_, StatusToString());
	if (status_ == kDisconnected) {
		// The connecting may be timeout, but the write event handler has been
		// dispatched in the EventLoop pending task queue, and next loop time the handle is invoked.
		// So we need to check the status whether it is at a kDisconnected
		ENGINE_LOG_INFO(engine::GetLogger(),
						"fd={} remote_addr={} receive write event when socket is closed",
						chan_->fd(),
						remote_addr_);
		return;
	}

	assert(status_ == kConnecting);
	int err = 0;
	socklen_t len = sizeof(err);
	if (getsockopt(chan_->fd(),
				   SOL_SOCKET,
				   SO_ERROR,
				   reinterpret_cast<char*>(&err),
				   reinterpret_cast<socklen_t*>(&len)) != 0) {
		err = EVPP_ERRNO;
		ENGINE_LOG_ERROR(engine::GetLogger(), "getsockopt failed err={} {}", err, strerror(err));
	}

	if (err != 0) {
		EVUTIL_SET_SOCKET_ERROR(err);
		HandleError();
		return;
	}

	assert(fd_ == chan_->fd());
	struct sockaddr_storage addr = sock::GetLocalAddr(chan_->fd());
	std::string laddr = sock::ToIPPort(&addr);
	conn_fn_(chan_->fd(), laddr);
	timer_->Cancel();
	timer_.reset();
	chan_->DisableAllEvent();
	chan_->Close();
	own_fd_ = false;  // Move the ownership of the fd to TCPConn
	fd_ = INVALID_SOCKET;
	status_ = kConnected;
}

void Connector::HandleError() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} {} status={}", (void*) this, remote_addr_, StatusToString());
	assert(loop_->IsInLoopThread());
	int serrno = EVPP_ERRNO;

	// In this error handling method, we will invoke 'conn_fn_' callback function
	// to notify the user application layer in which the user maybe call TCPClient::Disconnect.
	// TCPClient::Disconnect may cause this Connector object desctruct.
	auto self = shared_from_this();

	ENGINE_LOG_ERROR(engine::GetLogger(),
					 "this={} status={} fd={} use_count={} errno={} {}",
					 (void*) this,
					 StatusToString(),
					 fd_,
					 self.use_count(),
					 serrno,
					 strerror(serrno));

	status_ = kDisconnected;

	if (chan_) {
		assert(fd_ > 0);
		chan_->DisableAllEvent();
		chan_->Close();
	}

	// Avoid DNSResolver callback again when timeout
	if (dns_resolver_) {
		dns_resolver_->Cancel();
		dns_resolver_.reset();
	}

	if (timer_) {
		timer_->Cancel();
		timer_.reset();
	}

	if (reconnect_timer_) {
		reconnect_timer_->Cancel();
		reconnect_timer_.reset();
	}

	// Capture values before invoking user callback — the callback may
	// delete the TCPClient (owner_tcp_client_), making any subsequent
	// access to it a use-after-free.
	bool do_reconnect = owner_tcp_client_->auto_reconnect();
	Duration reconnect_interval = owner_tcp_client_->reconnect_interval();

	// If the connection is refused or it will not try again,
	// We need to notify the user layer that the connection established failed.
	// Otherwise we will try to do reconnection silently.
	if (EVUTIL_ERR_CONNECT_REFUSED(serrno) || !do_reconnect) {
		conn_fn_(-1, "");
	}

	// Although TCPClient has a Reconnect() method to deal with automatically reconnection problem,
	// TCPClient's Reconnect() will be invoked when a established connection is broken down.
	//
	// But if we could not connect to the remote server at the very beginning,
	// the TCPClient's Reconnect() will never be triggled.
	// So Connector needs to do reconnection automatically itself.
	if (do_reconnect) {
		// We must close(fd) firstly and then we can do the reconnection.
		if (fd_ > 0) {
			ENGINE_LOG_TRACE(
				engine::GetLogger(), "this={} Connector::HandleError close({})", (void*) this, fd_);
			assert(own_fd_);
			EVUTIL_CLOSESOCKET(fd_);
			fd_ = INVALID_SOCKET;
		}

		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} loop={} auto reconnect in {}s thread={}",
						 (void*) this,
						 (void*) loop_,
						 reconnect_interval.Seconds(),
						 std::hash<std::thread::id>{}(std::this_thread::get_id()));
		reconnect_timer_ =
			loop_->RunAfter(reconnect_interval, std::bind(&Connector::Start, shared_from_this()));
	}
}

void Connector::OnConnectTimeout() {
	ENGINE_LOG_WARN(engine::GetLogger(),
					"this={} Connector::OnConnectTimeout status={} fd={} this={}",
					(void*) this,
					StatusToString(),
					fd_,
					(void*) this);
	assert(status_ == kConnecting || status_ == kDNSResolving);
	EVUTIL_SET_SOCKET_ERROR(ETIMEDOUT);
	HandleError();
}

void Connector::OnDNSResolved(const std::vector<struct in_addr>& addrs) {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} addrs.size={} this={}",
					 (void*) this,
					 addrs.size(),
					 (void*) this);
	if (addrs.empty()) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "this={} DNS Resolve failed. host={}",
						 (void*) this,
						 dns_resolver_->host());
		HandleError();
		return;
	}

	struct sockaddr_in* addr = sock::sockaddr_in_cast(&raddr_);
	addr->sin_family = AF_INET;
	addr->sin_port = htons(remote_port_);
	addr->sin_addr = addrs[0];
	status_ = kDNSResolved;

	Connect();
}

std::string Connector::StatusToString() const {
	H_CASE_STRING_BIGIN(status_);
	H_CASE_STRING(kDisconnected);
	H_CASE_STRING(kDNSResolving);
	H_CASE_STRING(kDNSResolved);
	H_CASE_STRING(kConnecting);
	H_CASE_STRING(kConnected);
	H_CASE_STRING_END();
}
}
