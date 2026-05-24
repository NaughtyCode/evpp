#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>

#ifdef _WIN32
#include "tests/examples/winmain-inl.h"
#endif

#include "runtime/core/log/log.h"

typedef std::map<uint64_t, evpp::TCPConnPtr> ConnectionsMap;
typedef std::shared_ptr<ConnectionsMap> ConnectionsMapPtr;

std::atomic<int> g_connected = {0}; // The all connected connection count
std::atomic<int> g_current_round_connected = {0};
std::atomic<int> g_current_round_disconnected = {0};

std::atomic<int64_t> g_recved_message = {0};
std::atomic<int64_t> g_current_round_recved_message = {0};

std::atomic<int64_t> g_current_round_recved_bytes = {0};

void Print() {
    ENGINE_LOG_ERROR(engine::GetLogger(), "Running ...\n"
        "\t                Total connected {}\n"
        "\t        Current round connected {}\n"
        "\t     Current round disconnected {}\n"
        "\t        Total received messages {}\n"
        "\tCurrent round received messages {}\n"
        "\t                     Throughput {}MB/s\n",
        g_connected.load(),
        g_current_round_connected.exchange(0),
        g_current_round_disconnected.exchange(0),
        g_recved_message.load(),
        g_current_round_recved_message.exchange(0),
        g_current_round_recved_bytes.exchange(0) / 1024.0 / 1024.0);
}

void OnMessage(const evpp::TCPConnPtr& conn,
               evpp::Buffer* msg) {
    const size_t kHeadLen = 4;
    while (msg->size() >= kHeadLen) {
        int32_t len = msg->PeekInt32();
        if (msg->size() < len + kHeadLen) {
            break;
        }

        conn->Send(msg->data(), len + kHeadLen);
        g_current_round_recved_bytes += len + kHeadLen;
        g_recved_message++;
        g_current_round_recved_message++;
        msg->Skip(kHeadLen);
        std::string m = msg->NextString(len);
        bool check = true;
        for (auto i : m) {
            if (i != 'a') {
                check = false;
                break;
            }
        }
        if (!check) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Received an ERROR message.");
        }
    }
}

void OnConnection(const evpp::TCPConnPtr& conn) {
    if (conn->IsConnected()) {
        ENGINE_LOG_INFO(engine::GetLogger(), "Accept a new connection {}", conn->AddrToString());
        g_connected++;
        g_current_round_connected++;
    } else {
        ENGINE_LOG_INFO(engine::GetLogger(), "Disconnected from {}", conn->remote_addr());
        g_connected--;
        g_current_round_disconnected++;
    }
}

int main(int argc, char* argv[]) {
    std::string port = "9099";
    if (argc == 2) {
        port = argv[1];
    }
    std::string addr = std::string("0.0.0.0:") + port;
    evpp::EventLoop loop;
    loop.RunEvery(evpp::Duration(1.0), &Print);
    evpp::TCPServer server(&loop, addr, "c10m", 23);
    server.SetMessageCallback(&OnMessage);
    server.SetConnectionCallback(&OnConnection);
    server.Init();
    server.Start();
    loop.Run();
    return 0;
}
