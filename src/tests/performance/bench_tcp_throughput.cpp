#include "wsa_init.h"

#include <benchmark/benchmark.h>
#include <runtime/evpp/event_loop.h>
#include <runtime/evpp/buffer.h>
#include <runtime/evpp/tcp_conn.h>
#include <runtime/evpp/tcp_server.h>
#include <runtime/evpp/tcp_client.h>

#include <atomic>
#include <string>

// TCP localhost throughput — N messages of 1KB each
static void BM_TCP_Throughput(benchmark::State& state) {
    int msg_size = static_cast<int>(state.range(0));

    for (auto _ : state) {
        evpp::EventLoop loop;
        std::atomic<int> received{0};

        auto* server = new evpp::TCPServer(&loop, "127.0.0.1:0", "BmEcho", 1);
        server->SetMessageCallback([&](const evpp::TCPConnPtr& conn, evpp::Buffer* msg) {
            conn->Send(std::string(msg->data(), msg->length()));
        });
        server->Init();
        server->Start();

        auto addr = server->listen_addr();
        int port = std::stoi(addr.substr(addr.rfind(':') + 1));

        auto* client = new evpp::TCPClient(&loop,
            "127.0.0.1:" + std::to_string(port), "BmClient");
        client->set_auto_reconnect(false);

        std::string payload(msg_size, 'x');

        client->SetConnectionCallback([&](const evpp::TCPConnPtr& conn) {
            if (conn->IsConnected()) {
                conn->Send(payload);
            }
        });

        client->SetMessageCallback([&](const evpp::TCPConnPtr&, evpp::Buffer*) {
            received++;
            if (received >= 100) {
                loop.Stop();
            } else {
                client->conn()->Send(payload);
            }
        });

        loop.RunAfter(5.0, [&]() {
            loop.Stop();
        });
        client->Connect();
        loop.Run();

        client->Disconnect();
        server->Stop();
        delete client;
        delete server;

        state.SetBytesProcessed(
            static_cast<int64_t>(received.load()) * msg_size * 2);
    }
}
BENCHMARK(BM_TCP_Throughput)->Arg(1024)->Arg(65536);
