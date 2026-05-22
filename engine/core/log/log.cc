#include "engine/core/log/log.h"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>

#include "engine/core/log/log_config.h"

namespace engine {

quill::Logger* GetLogger(const std::string& name) {
    return quill::Frontend::get_logger(name);
}

void InitLogger(const std::string& log_dir) {
    std::string log_path = log_dir.empty() ? "logs" : log_dir;

    // Start backend thread (independent of evpp event loops)
    quill::Backend::start(GetBackendOptions());

    // Create sinks
#ifdef NDEBUG
    quill::RotatingFileSinkConfig file_cfg;
    file_cfg.set_rotation_max_file_size(100 * 1024 * 1024);
    file_cfg.set_max_backup_files(10);
    auto sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        log_path + "/engine.log", file_cfg);
#else
    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
#endif

    // Create root logger
    quill::Frontend::create_or_get_logger(
        "root", {sink},
        quill::PatternFormatterOptions{
            "%(time) [%(log_level_short_code)] [%(logger)] %(message)"});
}

void ShutdownLogger() {
    // Block until all SPSC queues are drained and written
    quill::Backend::stop();
}

} // namespace engine
