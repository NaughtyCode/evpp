#include <evpp/tcp_client.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#include "runtime/core/log/log.h"

int main(int argc, char* argv[]) {
    std::string addr = "127.0.0.1:9099";

    if (argc == 2) {
        addr = argv[1];
    }

    evpp::EventLoop loop;
    evpp::TCPClient client(&loop, addr, "TCPPingPongClient");
    client.SetMessageCallback([&loop, &client](const evpp::TCPConnPtr& conn,
                               evpp::Buffer* msg) {
        ENGINE_LOG_TRACE(engine::GetLogger(), "Receive a message [{}]", msg->ToString());
        client.Disconnect();
    });

    client.SetConnectionCallback([](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            ENGINE_LOG_INFO(engine::GetLogger(), "Connected to {}", conn->remote_addr());
            conn->Send("hello");
        } else {
            conn->loop()->Stop();
        }
    });
    client.Connect();
    loop.Run();
    return 0;
}

#include "tests/examples/winmain-inl.h"
