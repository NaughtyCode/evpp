#include "runtime/evpp/tcp_server.h"

#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/listener.h"
#include "runtime/evpp/tcp_conn.h"

namespace evpp {
TCPServer::TCPServer(EventLoop* loop,
					 const std::string& laddr,
					 const std::string& name,
					 uint32_t thread_num)
	: loop_(loop),
	  listen_addr_(laddr),
	  name_(name),
	  conn_fn_(&internal::DefaultConnectionCallback),
	  msg_fn_(&internal::DefaultMessageCallback),
	  next_conn_id_(0) {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} name={} listening addr {} thread_num={}",
					 (void*) this,
					 name,
					 laddr,
					 thread_num);
	tpool_.reset(new EventLoopThreadPool(loop_, thread_num));
}

TCPServer::~TCPServer() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(connections_.empty());
	assert(!listener_);
	if (tpool_) {
		assert(tpool_->IsStopped());
		tpool_.reset();
	}
}

bool TCPServer::Init() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(status_ == kNull);
	listener_.reset(new Listener(loop_, listen_addr_));
	if (!listener_->Listen()) {
		listener_.reset();
		return false;
	}
	status_.store(kInitialized);
	return true;
}

void TCPServer::AfterFork() {
	tpool_->AfterFork();
}

bool TCPServer::Start() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(status_ == kInitialized);
	status_.store(kStarting);
	assert(listener_.get());
	bool rc = tpool_->Start(true);
	if (rc) {
		assert(tpool_->IsRunning());
		listener_->SetNewConnectionCallback(std::bind(&TCPServer::HandleNewConn,
													  this,
													  std::placeholders::_1,
													  std::placeholders::_2,
													  std::placeholders::_3));

		// We must set status_ to kRunning firstly and then we can accept new
		// connections. If we use the following code :
		//     listener_->Accept();
		//     status_.store(kRunning);
		// there is a chance : we have accepted a connection but status_ is not
		// kRunning that will cause the assert(status_ == kRuning) failed in
		// TCPServer::HandleNewConn.
		status_.store(kRunning);
		listener_->Accept();
	}
	return rc;
}

void TCPServer::Stop(DoneCallback on_stopped_cb) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Entering ...", (void*) this);
	assert(status_ == kRunning);
	status_.store(kStopping);
	substatus_.store(kStoppingListener);
	loop_->RunInLoop(std::bind(&TCPServer::StopInLoop, this, on_stopped_cb));
}

void TCPServer::StopInLoop(DoneCallback on_stopped_cb) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Entering ...", (void*) this);
	assert(loop_->IsInLoopThread());
	listener_->Stop();
	listener_.reset();

	if (connections_.empty()) {
		// Stop all the working threads now.
		ENGINE_LOG_TRACE(engine::GetLogger(), "this={} no connections", (void*) this);
		StopThreadPool();
		if (on_stopped_cb) {
			on_stopped_cb();
			on_stopped_cb = DoneCallback();
		}
		status_.store(kStopped);
	} else {
		ENGINE_LOG_TRACE(engine::GetLogger(), "this={} close connections", (void*) this);
		for (auto& c : connections_) {
			if (c.second->IsConnected()) {
				ENGINE_LOG_TRACE(engine::GetLogger(),
								 "this={} close connection id={} fd={}",
								 (void*) this,
								 c.second->id(),
								 c.second->fd());
				c.second->Close();
			} else {
				ENGINE_LOG_TRACE(engine::GetLogger(),
								 "this={} Do not need to call Close for this TCPConn it may be "
								 "doing disconnecting. TCPConn={} fd={} status={}",
								 (void*) this,
								 (void*) c.second.get(),
								 c.second->fd(),
								 StatusToString());
			}
		}

		stopped_cb_ = on_stopped_cb;

		// The working threads will be stopped after all the connections closed.
	}

	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} exited, status={}", (void*) this, StatusToString());
}

void TCPServer::StopThreadPool() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} pool={}", (void*) this, (void*) tpool_.get());
	assert(loop_->IsInLoopThread());
	assert(IsStopping());
	substatus_.store(kStoppingThreadPool);
	tpool_->Stop(true);
	assert(tpool_->IsStopped());

	// Make sure all the working threads totally stopped.
	tpool_->Join();
	tpool_.reset();

	substatus_.store(kSubStatusNull);
}

void TCPServer::HandleNewConn(evpp_socket_t sockfd,
							  const std::string& remote_addr /*ip:port*/,
							  const struct sockaddr_in* raddr) {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} fd={}", (void*) this, sockfd);
	assert(loop_->IsInLoopThread());
	if (IsStopping()) {
		ENGINE_LOG_WARN(
			engine::GetLogger(),
			"this={} The server is at stopping status. Discard this socket fd={} remote_addr={}",
			(void*) this,
			sockfd,
			remote_addr);
		EVUTIL_CLOSESOCKET(sockfd);
		return;
	}

	assert(IsRunning());

	if (connection_count_ >= max_connections_) {
		ENGINE_LOG_WARN(engine::GetLogger(),
						"TCPServer '{}': connection limit reached ({}/{}). "
						"Rejecting new connection from {}.",
						name_,
						connection_count_.load(),
						max_connections_,
						remote_addr);
		EVUTIL_CLOSESOCKET(sockfd);
		return;
	}

	EventLoop* io_loop = GetNextLoop(raddr);
	++next_conn_id_;
	connection_count_++;
#ifdef H_DEBUG_MODE
	std::string n = name_ + "-" + remote_addr + "#" + std::to_string(next_conn_id_);
#else
	std::string n = remote_addr;
#endif
	TCPConnPtr conn(new TCPConn(io_loop, n, sockfd, listen_addr_, remote_addr, next_conn_id_));
	assert(conn->type() == TCPConn::kIncoming);
	conn->SetMessageCallback(msg_fn_);
	conn->SetConnectionCallback(conn_fn_);
	conn->SetCloseCallback(std::bind(&TCPServer::RemoveConnection, this, std::placeholders::_1));
	io_loop->RunInLoop(std::bind(&TCPConn::OnAttachedToLoop, conn));
	connections_[conn->id()] = conn;
}

EventLoop* TCPServer::GetNextLoop(const struct sockaddr_in* raddr) {
	if (IsRoundRobin()) {
		return tpool_->GetNextLoop();
	} else {
		return tpool_->GetNextLoopWithHash(raddr->sin_addr.s_addr);
	}
}

void TCPServer::RemoveConnection(const TCPConnPtr& conn) {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} conn={} fd={} connections_.size()={}",
					 (void*) this,
					 (void*) conn.get(),
					 conn->fd(),
					 connections_.size());
	auto f = [this, conn]() {
		// Remove the connection in the listening EventLoop
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} conn={} fd={} connections_.size()={}",
						 (void*) this,
						 (void*) conn.get(),
						 conn->fd(),
						 connections_.size());
		assert(this->loop_->IsInLoopThread());
		this->connections_.erase(conn->id());
		connection_count_--;
		if (IsStopping() && this->connections_.empty()) {
			// At last, we stop all the working threads
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} stop thread pool", (void*) this);
			assert(substatus_.load() == kStoppingListener);
			StopThreadPool();
			if (stopped_cb_) {
				stopped_cb_();
				stopped_cb_ = DoneCallback();
			}
			status_.store(kStopped);
		}
	};
	loop_->RunInLoop(f);
}

}
