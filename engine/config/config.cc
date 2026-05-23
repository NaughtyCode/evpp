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

bool ConfigManager::Load(const std::string& config_dir) {
    // Logger is not initialized yet — use stderr for error reporting.
    // Success messages are logged later by Engine::Init after InitLogger.

    // ── engine.json ──────────────────────────────────────────────────
    std::string engine_path = config_dir + "/engine.json";
    {
        std::string buf;
        auto ec = glz::read_file_json(engine_config_, engine_path, buf);
        if (ec) {
            std::string err = glz::format_error(ec, buf);
            std::fprintf(stderr, "ConfigManager: failed to load [%s]: %s\n",
                         engine_path.c_str(), err.c_str());
            return false;
        }
    }

    // ── server.json ──────────────────────────────────────────────────
    std::string server_path = config_dir + "/server.json";
    {
        std::string buf;
        auto ec = glz::read_file_json(server_config_, server_path, buf);
        if (ec) {
            std::string err = glz::format_error(ec, buf);
            std::fprintf(stderr, "ConfigManager: failed to load [%s]: %s\n",
                         server_path.c_str(), err.c_str());
            return false;
        }
    }

    return true;
}

bool ConfigManager::Reload(const std::string& config_dir) {
    // Reload happens at runtime — logger is available.
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
