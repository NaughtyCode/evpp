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
#include "runtime/config/config_constants.h"
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

    // Load config from JSON files (config path is fixed; resource_dir
    // in the config controls where scripts/physics/etc. live).
    // CLI override: --config_dir= can change the config location.
    std::string config_dir(engine::config::kConfigDir);
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--config_dir=", 0) == 0) {
            config_dir = arg.substr(13);
        }
    }
    if (!engine::ConfigManager::Instance().Load(config_dir)) {
        std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":3,"
                  << "\"reason\":\"config_load_failed\","
                  << "\"config_dir\":\"" << config_dir << "\"}" << std::endl;
        return 3;
    }
    std::fprintf(stderr, "[main] config loaded\n");

    // CLI arguments override config values (thread-safe via copy-modify-set)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log_dir=", 0) == 0) {
            auto rt = engine::ConfigManager::Instance().GetRuntimeConfig();
            rt.log.dir = arg.substr(10);
            engine::ConfigManager::Instance().SetRuntimeOverride(rt);
        } else if (arg.rfind("--scripts_dir=", 0) == 0) {
            auto srv = engine::ConfigManager::Instance().GetServerConfig();
            srv.scripts_dir = arg.substr(14);
            engine::ConfigManager::Instance().SetServerOverride(srv);
        }
    }

    std::fprintf(stderr, "[main] calling Engine::Init()\n");
    auto& engine = engine::Engine::Instance();
    const auto& runtime_cfg = engine::ConfigManager::Instance().GetRuntimeConfig();
    const auto& server_cfg = engine::ConfigManager::Instance().GetServerConfig();
    engine.Init(runtime_cfg, server_cfg.scripts_dir);
    std::fprintf(stderr, "[main] Engine::Init() returned, calling Engine::Run()\n");
    engine.Run();

    engine::ShutdownLogger();
    return 0;
}
