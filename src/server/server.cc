#include <cstdio>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

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
#include "runtime/config/config_validator.h"
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

std::string ProgramName(const char* argv0) {
    std::filesystem::path path(argv0 ? argv0 : "");
    std::string name = path.stem().string();
    return name.empty() ? "GameServer" : name;
}

struct ServerCliOptions {
    engine::Environment active_env = engine::Environment::development;
    std::string config_dir;
    std::string log_prefix;
    bool log_prefix_explicit = false;
    std::string log_dir;
    bool log_dir_explicit = false;
    std::string scripts_dir;
    bool scripts_dir_explicit = false;
    std::string instance_id;
    bool instance_id_explicit = false;
    std::string pid_file;
    bool pid_file_explicit = false;
    bool disable_pid_file = false;
    int admin_port = 0;
    bool admin_port_explicit = false;
    std::string admin_bind_address;
    bool admin_bind_address_explicit = false;
    std::string admin_auth_token;
    bool admin_auth_token_explicit = false;
};

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool ParseInt(std::string_view text, int* out) {
    if (text.empty() || out == nullptr) {
        return false;
    }
    int value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    *out = value;
    return true;
}

std::string SafePathComponent(std::string_view value) {
    std::string safe;
    safe.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            safe.push_back(static_cast<char>(ch));
        } else {
            safe.push_back('_');
        }
    }
    return safe.empty() ? "instance" : safe;
}

std::string AddPathSuffix(const std::string& path, const std::string& suffix) {
    if (path.empty() || suffix.empty()) {
        return path;
    }

    std::filesystem::path p(path);
    std::string stem = p.stem().string();
    std::string extension = p.extension().string();
    if (stem.empty()) {
        stem = p.filename().string();
        extension.clear();
    }
    if (stem.empty()) {
        stem = "server";
    }

    std::filesystem::path filename(stem + "-" + suffix + extension);
    auto parent = p.parent_path();
    return parent.empty() ? filename.string() : (parent / filename).string();
}

bool ParseCli(int argc, char* argv[], ServerCliOptions* out, std::string* error_out) {
    if (out == nullptr) {
        return false;
    }

    out->active_env = engine::EnvironmentFromEnvVar();
    out->config_dir = engine::config::kConfigDir;
    out->log_prefix = ProgramName(argc > 0 ? argv[0] : nullptr);

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i] ? argv[i] : "");
        if (StartsWith(arg, "--env=")) {
            out->active_env = engine::ParseEnvironment(std::string(arg.substr(6)));
        } else if (StartsWith(arg, "--config_dir=")) {
            out->config_dir = std::string(arg.substr(13));
        } else if (StartsWith(arg, "--log_prefix=")) {
            out->log_prefix = std::string(arg.substr(13));
            out->log_prefix_explicit = true;
        } else if (StartsWith(arg, "--log_dir=")) {
            out->log_dir = std::string(arg.substr(10));
            out->log_dir_explicit = true;
        } else if (StartsWith(arg, "--scripts_dir=")) {
            out->scripts_dir = std::string(arg.substr(14));
            out->scripts_dir_explicit = true;
        } else if (StartsWith(arg, "--instance_id=")) {
            out->instance_id = std::string(arg.substr(14));
            out->instance_id_explicit = true;
        } else if (StartsWith(arg, "--pid_file=")) {
            out->pid_file = std::string(arg.substr(11));
            out->pid_file_explicit = true;
        } else if (arg == "--disable_pid_file") {
            out->disable_pid_file = true;
        } else if (StartsWith(arg, "--admin_port=")) {
            int value = 0;
            if (!ParseInt(arg.substr(13), &value) || value < 0 || value > 65535) {
                if (error_out) {
                    *error_out = "invalid --admin_port, expected 0..65535";
                }
                return false;
            }
            out->admin_port = value;
            out->admin_port_explicit = true;
        } else if (StartsWith(arg, "--admin_bind_address=")) {
            out->admin_bind_address = std::string(arg.substr(21));
            out->admin_bind_address_explicit = true;
        } else if (StartsWith(arg, "--admin_auth_token=")) {
            out->admin_auth_token = std::string(arg.substr(19));
            out->admin_auth_token_explicit = true;
        }
    }

    if (out->log_prefix.empty()) {
        out->log_prefix = ProgramName(argc > 0 ? argv[0] : nullptr);
    }
    return true;
}

std::string ResolveInstanceId(const ServerCliOptions& cli,
                              const engine::ServerConfig& server_cfg) {
    return cli.instance_id_explicit ? cli.instance_id : server_cfg.instance.id;
}

std::string ResolveLogPrefix(const ServerCliOptions& cli,
                             const std::string& resolved_instance_id) {
    if (cli.log_prefix_explicit || resolved_instance_id.empty()) {
        return cli.log_prefix;
    }
    return cli.log_prefix + "-" + SafePathComponent(resolved_instance_id);
}

std::string ResolvePidPath(const ServerCliOptions& cli,
                           const std::string& configured_pid_file,
                           const std::string& resolved_instance_id) {
    if (cli.disable_pid_file) {
        return "";
    }
    if (cli.pid_file_explicit) {
        return cli.pid_file;
    }
    if (!resolved_instance_id.empty()) {
        return AddPathSuffix(configured_pid_file, SafePathComponent(resolved_instance_id));
    }
    return configured_pid_file;
}

bool ValidateEffectiveConfig(const engine::RuntimeConfig& runtime_cfg,
                             const engine::ServerConfig& server_cfg,
                             std::string* error_out) {
    auto server_result = engine::ConfigValidator::ValidateServer(server_cfg);
    if (!server_result.valid) {
        if (error_out) *error_out = server_result.errors;
        return false;
    }
    auto cross_result = engine::ConfigValidator::ValidateCross(runtime_cfg, server_cfg);
    if (!cross_result.valid) {
        if (error_out) *error_out = cross_result.errors;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    engine::SetCurrentThreadName("MainThread");

    std::fprintf(stderr, "[main] starting\n");
    WinSockGuard winsock_guard;

    ServerCliOptions cli;
    std::string cli_error;
    if (!ParseCli(argc, argv, &cli, &cli_error)) {
        std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                  << static_cast<int>(engine::ExitCode::ConfigParseError) << ","
                  << "\"reason\":\"invalid_cli_arg\","
                  << "\"error\":\"" << cli_error << "\"}" << std::endl;
        return static_cast<int>(engine::ExitCode::ConfigParseError);
    }
    std::fprintf(stderr, "[main] environment: %s\n",
                 engine::EnvironmentToString(cli.active_env));

    // Set environment before Load() so profile layering works.
    engine::ConfigManager::Instance().SetActiveEnvironment(cli.active_env);

    // Check if config directory exists (ConfigNotFound vs ConfigParseError).
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(cli.config_dir, ec)) {
            std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                      << static_cast<int>(engine::ExitCode::ConfigNotFound) << ","
                      << "\"reason\":\"config_dir_not_found\","
                      << "\"config_dir\":\"" << cli.config_dir << "\"}" << std::endl;
            return static_cast<int>(engine::ExitCode::ConfigNotFound);
        }
    }

    if (!engine::ConfigManager::Instance().Load(cli.config_dir)) {
        std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                  << static_cast<int>(engine::ExitCode::ConfigParseError) << ","
                  << "\"reason\":\"config_load_failed\","
                  << "\"config_dir\":\"" << cli.config_dir << "\"}" << std::endl;
        return static_cast<int>(engine::ExitCode::ConfigParseError);
    }
    std::fprintf(stderr, "[main] config loaded\n");

    auto rt = engine::ConfigManager::Instance().GetRuntimeConfig();
    auto srv = engine::ConfigManager::Instance().GetServerConfig();
    const std::string resolved_instance_id = ResolveInstanceId(cli, srv);

    if (cli.instance_id_explicit) {
        srv.instance.id = cli.instance_id;
    }
    if (cli.scripts_dir_explicit) {
        srv.scripts_dir = cli.scripts_dir;
    }
    if (cli.admin_port_explicit) {
        srv.admin_port = cli.admin_port;
    }
    if (cli.admin_bind_address_explicit) {
        srv.admin_bind_address = cli.admin_bind_address;
    }
    if (cli.admin_auth_token_explicit) {
        srv.admin_auth_token = cli.admin_auth_token;
    }

    rt.log.log_filename = ResolveLogPrefix(cli, resolved_instance_id);
    if (cli.log_dir_explicit) {
        rt.log.dir = cli.log_dir;
    }

    // ── PID file ────────────────────────────────────────────────────────
    const std::string pid_path = ResolvePidPath(cli, srv.pid_file, resolved_instance_id);
    srv.pid_file = pid_path;

    std::string validation_error;
    if (!ValidateEffectiveConfig(rt, srv, &validation_error)) {
        std::cerr << "{\"event\":\"startup_failed\",\"exit_code\":"
                  << static_cast<int>(engine::ExitCode::ConfigValidationError) << ","
                  << "\"reason\":\"config_validation_failed\","
                  << "\"error\":\"" << validation_error << "\"}" << std::endl;
        return static_cast<int>(engine::ExitCode::ConfigValidationError);
    }
    engine::ConfigManager::Instance().SetRuntimeOverride(rt);
    engine::ConfigManager::Instance().SetServerOverride(srv);

    // With --instance_id, default server.pid becomes server-<instance>.pid.
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
