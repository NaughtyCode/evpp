#include "runtime/config/config.h"

#include <cstdio>
#include <filesystem>

#include <glaze/glaze.hpp>

#include "runtime/core/log/log.h"

namespace engine {

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

//============================================================================
// From JSON strings (text)
//============================================================================

bool ConfigManager::LoadRuntimeFromString(const std::string& json) {
    auto ec = glz::read_json(runtime_config_, json);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to parse runtime config: %s\n",
                     glz::format_error(ec, json).c_str());
        return false;
    }
    return true;
}

bool ConfigManager::LoadClientFromString(const std::string& json) {
    auto ec = glz::read_json(client_config_, json);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to parse client config: %s\n",
                     glz::format_error(ec, json).c_str());
        return false;
    }
    return true;
}

bool ConfigManager::LoadServerFromString(const std::string& json) {
    auto ec = glz::read_json(server_config_, json);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to parse server config: %s\n",
                     glz::format_error(ec, json).c_str());
        return false;
    }
    return true;
}

//============================================================================
// From files
//============================================================================

bool ConfigManager::LoadRuntimeFromFile(const std::string& path) {
    std::string buf;
    auto ec = glz::read_file_json(runtime_config_, path, buf);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to load [%s]: %s\n",
                     path.c_str(), glz::format_error(ec, buf).c_str());
        return false;
    }
    return true;
}

bool ConfigManager::LoadClientFromFile(const std::string& path) {
    std::string buf;
    auto ec = glz::read_file_json(client_config_, path, buf);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to load [%s]: %s\n",
                     path.c_str(), glz::format_error(ec, buf).c_str());
        return false;
    }
    return true;
}

bool ConfigManager::LoadServerFromFile(const std::string& path) {
    std::string buf;
    auto ec = glz::read_file_json(server_config_, path, buf);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to load [%s]: %s\n",
                     path.c_str(), glz::format_error(ec, buf).c_str());
        return false;
    }
    return true;
}

bool ConfigManager::Load(const std::string& config_dir) {
    // Runtime config is required — both client and server need it.
    std::string runtime_path = config_dir + "/runtime/runtime.json";
    if (!LoadRuntimeFromFile(runtime_path)) return false;

    // Client and server configs are optional — one may not exist
    // depending on the build target.  Check existence first to avoid
    // spurious "failed to load" messages on stderr.
    std::error_code ec;
    std::string client_path = config_dir + "/client/client.json";
    if (std::filesystem::exists(client_path, ec)) {
        LoadClientFromFile(client_path);
    }
    std::string server_path = config_dir + "/server/server.json";
    if (std::filesystem::exists(server_path, ec)) {
        LoadServerFromFile(server_path);
    }
    return true;
}

//============================================================================
// Reload (runtime — logger is available)
//============================================================================

bool ConfigManager::Reload(const std::string& config_dir) {
    auto* logger = GetLogger();

    RuntimeConfig new_runtime;
    ClientConfig new_client;
    ServerConfig new_server;

    std::string buf;
    auto ec = glz::read_file_json(new_runtime, config_dir + "/runtime/runtime.json", buf);
    if (ec) {
        ENGINE_LOG_ERROR(logger, "ConfigManager: reload failed for runtime.json: {}",
                         glz::format_error(ec, buf));
        return false;
    }

    runtime_config_ = std::move(new_runtime);

    std::error_code ec2;
    std::string client_path = config_dir + "/client/client.json";
    if (std::filesystem::exists(client_path, ec2)) {
        buf.clear();
        auto ec3 = glz::read_file_json(new_client, client_path, buf);
        if (!ec3) {
            client_config_ = std::move(new_client);
        }
    }

    std::string server_path = config_dir + "/server/server.json";
    if (std::filesystem::exists(server_path, ec2)) {
        buf.clear();
        auto ec3 = glz::read_file_json(new_server, server_path, buf);
        if (!ec3) {
            server_config_ = std::move(new_server);
        }
    }

    ENGINE_LOG_INFO(logger, "ConfigManager: config reloaded");
    return true;
}

} // namespace engine
