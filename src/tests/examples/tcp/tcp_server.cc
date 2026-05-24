#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#include "runtime/core/log/log.h"

int main(int argc, char* argv[]) {
    std::string addr = "0.0.0.0:9099";
    int thread_num = 4;
    evpp::EventLoop loop;
    evpp::TCPServer server(&loop, addr, "TCPEchoServer", thread_num);
    server.SetMessageCallback([](const evpp::TCPConnPtr& conn,
                                 evpp::Buffer* msg) {
        conn->Send(msg);
    });
    server.SetConnectionCallback([](const evpp::TCPConnPtr& conn) {
        if (conn->IsConnected()) {
            ENGINE_LOG_INFO(engine::GetLogger(), "A new connection from {}", conn->remote_addr());
        } else {
            ENGINE_LOG_INFO(engine::GetLogger(), "Lost the connection from {}", conn->remote_addr());
        }
    });
    server.Init();
    server.Start();
    loop.Run();
    return 0;
}

#include "tests/examples/winmain-inl.h"

