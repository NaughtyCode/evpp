#include <atomic>

#include "runtime/evpp/inner_pre.h"

#include "runtime/evpp/tcp_client.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/tcp_conn.h"
#include "runtime/evpp/fd_channel.h"
#include "runtime/evpp/connector.h"

namespace evpp {
static std::atomic<uint64_t> id;
TCPClient::TCPClient(EventLoop* l, const std::string& raddr, const std::string& n)
    : loop_(l)
    , remote_addr_(raddr)
    , name_(n)
    , conn_fn_(&internal::DefaultConnectionCallback)
    , msg_fn_(&internal::DefaultMessageCallback) {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} remote addr={}", (void*)this, raddr);
}

TCPClient::~TCPClient() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*)this);
    assert(!connector_.get());
    auto_reconnect_.store(false);
    TCPConnPtr c = conn();
    if (c) {
        // Most of the cases, the conn_ is at disconnected status at this time.
        // But some times, the user application layer will call TCPClient::Close()
        // and delete TCPClient object immediately, that will make conn_ to be at disconnecting status.
        assert(c->IsDisconnected() || c->IsDisconnecting());
        if (c->IsDisconnecting()) {
            // the reference count includes :
            //  - this
            //  - c
            //  - A disconnecting callback which hold a shared_ptr of TCPConn
            assert(c.use_count() >= 3);
            c->SetCloseCallback(CloseCallback());
        }
    }
    conn_.reset();
}

void TCPClient::Bind(const std::string& addr/*host:port*/) {
    local_addr_ = addr;
}

void TCPClient::Connect() {
    ENGINE_LOG_INFO(engine::GetLogger(), "remote_addr={}", remote_addr());
    auto f = [this]() {
        assert(loop_->IsInLoopThread());
        connector_.reset(new Connector(loop_, this));
        connector_->SetNewConnectionCallback(std::bind(&TCPClient::OnConnection, this, std::placeholders::_1, std::placeholders::_2));
        connector_->Start();
    };
    loop_->RunInLoop(f);
}

void TCPClient::Disconnect() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*)this);
    loop_->RunInLoop(std::bind(&TCPClient::DisconnectInLoop, this));
}

void TCPClient::DisconnectInLoop() {
    ENGINE_LOG_WARN(engine::GetLogger(), "TCPClient::DisconnectInLoop this={} remote_addr={}", (void*)this, remote_addr_);
    assert(loop_->IsInLoopThread());
    auto_reconnect_.store(false);

    if (conn_) {
        ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Close the TCPConn {} status={}", (void*)this, (void*)conn_.get(), conn_->StatusToString());
        assert(!conn_->IsDisconnected() && !conn_->IsDisconnecting());
        conn_->Close();
    }

    if (connector_) {
        if (connector_->IsConnected() || connector_->IsDisconnected()) {
            ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Nothing to do with connector_, Connector::status={}", (void*)this, connector_->status());
        } else {
            // When connector_ is trying to connect to the remote server we should cancel it to release the resources.
            connector_->Cancel();
        }
        connector_.reset(); // Free connector_ in loop thread immediately
    }
}

void TCPClient::Reconnect() {
    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Try to reconnect to {} in {}s again", (void*)this, remote_addr_, reconnect_interval_.Seconds());
    Connect();
}

void TCPClient::SetConnectionCallback(const ConnectionCallback& cb) {
    conn_fn_ = cb;
    auto  c = conn();
    if (c) {
        c->SetConnectionCallback(cb);
    }
}

void TCPClient::OnConnection(evpp_socket_t sockfd, const std::string& laddr) {
    if (sockfd < 0) {
        ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Failed to connect to {}. errno={} {}", (void*)this, remote_addr_, EVPP_ERRNO, strerror(EVPP_ERRNO));
        // We need to notify this failure event to the user layer
        // Note: When we could not connect to a server,
        //       the user layer will receive this notification constantly
        //       because the connector_ will retry to do reconnection all the time.
        conn_fn_(TCPConnPtr(new TCPConn(loop_, "", sockfd, laddr, remote_addr_, 0)));
        return;
    }

    ENGINE_LOG_TRACE(engine::GetLogger(), "this={} Successfully connected to {}", (void*)this, remote_addr_);
    assert(loop_->IsInLoopThread());
    TCPConnPtr c = TCPConnPtr(new TCPConn(loop_, name_, sockfd, laddr, remote_addr_, id++));
    c->set_type(TCPConn::kOutgoing);
    c->SetMessageCallback(msg_fn_);
    c->SetConnectionCallback(conn_fn_);
    c->SetCloseCallback(std::bind(&TCPClient::OnRemoveConnection, this, std::placeholders::_1));

    {
        std::lock_guard<std::mutex> guard(mutex_);
        conn_ = c;
    }

    c->OnAttachedToLoop();
}

void TCPClient::OnRemoveConnection(const TCPConnPtr& c) {
    assert(c.get() == conn_.get());
    assert(loop_->IsInLoopThread());
    conn_.reset();
    if (auto_reconnect_.load()) {
        Reconnect();
    }
}

TCPConnPtr TCPClient::conn() const {
    if (loop_->IsInLoopThread()) {
        return conn_;
    } else {
        // If it is not in the loop thread, we should add a lock here
        std::lock_guard<std::mutex> guard(mutex_);
        TCPConnPtr c = conn_;
        return c;
    }
}
}
