#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"
#include "engine/engine/engine.h"

namespace {

struct WinSockGuard {
    WinSockGuard() {
#ifdef _WIN32
        WSADATA wsa_data;
        int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (err) {
            std::cerr << "WSAStartup() failed with error: " << err << std::endl;
        }
#endif
    }
    ~WinSockGuard() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

} // namespace

int main(int argc, char* argv[]) {
    WinSockGuard winsock_guard;

    std::string log_dir = "logs";
    std::string scripts_dir = "resources/script";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log_dir=", 0) == 0) {
            log_dir = arg.substr(10);
        } else if (arg.rfind("--scripts_dir=", 0) == 0) {
            scripts_dir = arg.substr(15);
        }
    }

    auto& engine = engine::Engine::Instance();
    engine.Init(log_dir, scripts_dir);
    engine.Run();

    engine::ShutdownLogger();
    return 0;
}
