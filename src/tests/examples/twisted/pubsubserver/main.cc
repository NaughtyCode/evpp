#include <set>

#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#include "runtime/core/log/log.h"

#include "tests/examples/winmain-inl.h"

// Example from http://twistedmatrix.com/trac/#pubsubserver

class Server {
public:
    Server(int port) {
        std::string addr = std::string("0.0.0.0:") + std::to_string(port);
        loop_.reset(new evpp::EventLoop);
        server_.reset(new evpp::TCPServer(loop_.get(), addr, "ChatRoom", 0));
        server_->SetMessageCallback(std::bind(&Server::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
        server_->SetConnectionCallback(std::bind(&Server::OnConnection, this, std::placeholders::_1));
    }

    void Run() {
        bool rc = server_->Init();
        rc = rc && server_->Start();
        assert(rc);
        loop_->Run();
    }

private:
    void OnMessage(const evpp::TCPConnPtr& conn,
                   evpp::Buffer* msg) {
        std::string s = msg->NextAllString();
        ENGINE_LOG_INFO(engine::GetLogger(), "Received a message [{}]", s);
        if (s == "quit" || s == "exit") {
            conn->Close();
        }

        for (auto &c : conns_) {
            c->Send(s);
        }
    }

    void OnConnection(const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            ENGINE_LOG_INFO(engine::GetLogger(), "A new connection from {} to {} is UP", conn->remote_addr(), server_->listen_addr());
            conns_.insert(conn);
        } else {
            ENGINE_LOG_INFO(engine::GetLogger(), "Disconnected from {}", conn->remote_addr());
            conns_.erase(conn);
        }
    }

private:
    std::shared_ptr<evpp::EventLoop> loop_;
    std::shared_ptr<evpp::TCPServer> server_;
    std::set<evpp::TCPConnPtr> conns_;
};

int main(int argc, char* argv[]) {
    int port = 1025;
    if (argc == 2) {
        port = std::atoi(argv[1]);
    }

    Server s(port);
    s.Run();
    return 0;
}
