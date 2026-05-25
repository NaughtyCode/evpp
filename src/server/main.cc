#include <cstdio>
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

#include "runtime/config/config.h"
#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/engine/engine.h"

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
    std::fprintf(stderr, "[main] starting\n");
    WinSockGuard winsock_guard;

    // Load config from JSON files
    std::string config_dir = "resources/config";
    if (!engine::ConfigManager::Instance().Load(config_dir)) {
        std::cerr << "Failed to load config from " << config_dir << std::endl;
        return 1;
    }
    std::fprintf(stderr, "[main] config loaded\n");

    // CLI arguments override config values
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log_dir=", 0) == 0) {
            engine::ConfigManager::Instance()
                .GetEngineConfigMutable().log.dir = arg.substr(10);
        } else if (arg.rfind("--scripts_dir=", 0) == 0) {
            engine::ConfigManager::Instance()
                .GetEngineConfigMutable().scripts_dir = arg.substr(15);
        }
    }

    std::fprintf(stderr, "[main] calling Engine::Init()\n");
    auto& engine = engine::Engine::Instance();
    if (!engine.Init(engine::ConfigManager::Instance().GetEngineConfig())) {
        std::cerr << "Engine::Init() failed" << std::endl;
        return 1;
    }
    std::fprintf(stderr, "[main] Engine::Init() returned, calling Engine::Run()\n");
    engine.Run();

    engine::ShutdownLogger();
    return 0;
}
