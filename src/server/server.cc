#include <cstdio>
#include <filesystem>
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
#include "runtime/core/exit_code.h"
#include "runtime/core/log/log.h"
#include "runtime/core/log/log_macros.h"
#include "runtime/core/pid_file.h"
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

    // Determine deployment environment (highest priority first):
    //   1. --env=<name> CLI flag
    //   2. EVPP_ENV environment variable
    //   3. Default: development (set by ConfigManager constructor)
    engine::Environment active_env = engine::EnvironmentFromEnvVar();
    std::string config_dir(engine::config::kConfigDir);
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--env=", 0) == 0) {
            active_env = engine::ParseEnvironment(arg.substr(6));
        } else if (arg.rfind("--config_dir=", 0) == 0) {
            config_dir = arg.substr(13);
        }
    }
    std::fprintf(stderr, "[main] environment: %s\n",
                 engine::EnvironmentToString(active_env));

    // Set environment before Load() so profile layering works.
    engine::ConfigManager::Instance().SetActiveEnvironment(active_env);

    // Check if config directory exists (ConfigNotFound vs ConfigParseError).
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(config_dir, ec)) {
            std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                      << static_cast<int>(engine::ExitCode::ConfigNotFound) << ","
                      << "\"reason\":\"config_dir_not_found\","
                      << "\"config_dir\":\"" << config_dir << "\"}" << std::endl;
            return static_cast<int>(engine::ExitCode::ConfigNotFound);
        }
    }

    if (!engine::ConfigManager::Instance().Load(config_dir)) {
        std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                  << static_cast<int>(engine::ExitCode::ConfigParseError) << ","
                  << "\"reason\":\"config_load_failed\","
                  << "\"config_dir\":\"" << config_dir << "\"}" << std::endl;
        return static_cast<int>(engine::ExitCode::ConfigParseError);
    }
    std::fprintf(stderr, "[main] config loaded\n");

    // ── PID file ────────────────────────────────────────────────────────
    std::string pid_path;
    {
        auto sc = engine::ConfigManager::Instance().GetServerConfig();
        pid_path = sc.pid_file;
    }
    if (!pid_path.empty()) {
        std::string pid_err;
        if (!engine::PidFile::WritePidFile(pid_path, &pid_err)) {
            std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                      << static_cast<int>(engine::ExitCode::MultiInstance) << ","
                      << "\"reason\":\"pid_file_locked\","
                      << "\"pid_file\":\"" << pid_path << "\","
                      << "\"error\":\"" << pid_err << "\"}" << std::endl;
            return static_cast<int>(engine::ExitCode::MultiInstance);
        }
        std::fprintf(stderr, "[main] PID file written: %s\n", pid_path.c_str());
    }

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

    auto& engine = engine::Engine::Instance();
    const auto& runtime_cfg = engine::ConfigManager::Instance().GetRuntimeConfig();
    const auto& server_cfg = engine::ConfigManager::Instance().GetServerConfig();

    std::fprintf(stderr, "[main] instance=[%s] calling Engine::Init()\n",
                 server_cfg.instance.id.empty() ? "(unset)" : server_cfg.instance.id.c_str());
    engine.Init(runtime_cfg, server_cfg.scripts_dir);
    std::fprintf(stderr, "[main] Engine::Init() returned, calling Engine::Run()\n");
    engine.Run();

    engine::ShutdownLogger();

    // Remove PID file on clean shutdown.
    if (!pid_path.empty()) {
        engine::PidFile::RemovePidFile(pid_path);
        std::fprintf(stderr, "[main] PID file removed: %s\n", pid_path.c_str());
    }

    return static_cast<int>(engine::ExitCode::Success);
}
