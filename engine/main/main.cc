#include <memory>
#include <string>
#include <iostream>

#include <evpp/event_loop.h>
#include <evpp/event_watcher.h>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

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
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log_dir=", 0) == 0) {
            log_dir = arg.substr(10);
        }
    }

    engine::InitLogger(log_dir);

    auto* logger = engine::GetLogger();
    ENGINE_LOG_INFO(logger, "engine starting up, log_dir=[{}]", log_dir);

    evpp::EventLoop loop;

    auto sigint_watcher = std::make_unique<evpp::SignalEventWatcher>(
        SIGINT, &loop, [&loop]() {
            ENGINE_LOG_INFO(engine::GetLogger(), "SIGINT received, stopping...");
            loop.Stop();
        });
    sigint_watcher->Init();
    sigint_watcher->AsyncWait();

#ifndef _WIN32
    auto sigterm_watcher = std::make_unique<evpp::SignalEventWatcher>(
        SIGTERM, &loop, [&loop]() {
            ENGINE_LOG_INFO(engine::GetLogger(), "SIGTERM received, stopping...");
            loop.Stop();
        });
    sigterm_watcher->Init();
    sigterm_watcher->AsyncWait();
#endif

    ENGINE_LOG_INFO(logger, "entering main loop");
    loop.Run();
    ENGINE_LOG_INFO(logger, "main loop exited");

    sigint_watcher.reset();
#ifndef _WIN32
    sigterm_watcher.reset();
#endif

    engine::ShutdownLogger();
    return 0;
}
