#include "engine/config/config.h"

#include <cstdio>

#include <glaze/glaze.hpp>

#include "engine/core/log/log.h"
#include "engine/core/log/log_macros.h"

namespace engine {

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

//============================================================================
// From JSON strings (text)
//============================================================================

bool ConfigManager::LoadEngineFromString(const std::string& json) {
    auto ec = glz::read_json(engine_config_, json);
    if (ec) {
        std::fprintf(stderr, "ConfigManager: failed to parse engine config: %s\n",
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

bool ConfigManager::LoadFromString(const std::string& engine_json,
                                   const std::string& server_json) {
    if (!LoadEngineFromString(engine_json)) return false;
    if (!LoadServerFromString(server_json)) return false;
    return true;
}

//============================================================================
// From files
//============================================================================

bool ConfigManager::LoadEngineFromFile(const std::string& path) {
    std::string buf;
    auto ec = glz::read_file_json(engine_config_, path, buf);
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
    // Logger is not initialized yet — use stderr for error reporting.
    // Success messages are logged later by Engine::Init after InitLogger.
    std::string engine_path = config_dir + "/engine.json";
    std::string server_path = config_dir + "/server.json";
    if (!LoadEngineFromFile(engine_path)) return false;
    if (!LoadServerFromFile(server_path)) return false;
    return true;
}

//============================================================================
// Reload (runtime — logger is available)
//============================================================================

bool ConfigManager::Reload(const std::string& config_dir) {
    auto* logger = GetLogger();

    EngineConfig new_engine;
    ServerConfig new_server;

    std::string buf;
    auto ec = glz::read_file_json(new_engine, config_dir + "/engine.json", buf);
    if (ec) {
        ENGINE_LOG_ERROR(logger, "ConfigManager: reload failed for engine.json: {}",
                         glz::format_error(ec, buf));
        return false;
    }
    buf.clear();
    ec = glz::read_file_json(new_server, config_dir + "/server.json", buf);
    if (ec) {
        ENGINE_LOG_ERROR(logger, "ConfigManager: reload failed for server.json: {}",
                         glz::format_error(ec, buf));
        return false;
    }

    engine_config_ = std::move(new_engine);
    server_config_ = std::move(new_server);

    ENGINE_LOG_INFO(logger, "ConfigManager: config reloaded");
    return true;
}

} // namespace engine
