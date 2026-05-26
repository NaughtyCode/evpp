#pragma once

#include <string>

namespace engine {

struct DbLogConfig {
    std::string dir = "logs/db_service";
    std::string level = "info";
    int rotation_size_mb = 100;
    int max_backup_files = 10;
};

struct DbScriptConfig {
    std::string runtime_scripts_dir = "resources/script/runtime";
    std::string db_scripts_dir = "resources/script/db_service";
    bool        auto_load = true;
};

struct DbThreadPoolConfig {
    int thread_count = 4;
    int request_queue_size = 1024;
    int response_queue_size = 1024;
};

struct DbConnectionPoolConfig {
    int max_pool_size = 16;
    int wait_queue_timeout_ms = 5000;
};

struct DbServiceConfig {
    DbLogConfig log;
    DbThreadPoolConfig thread_pool;
    DbConnectionPoolConfig connection_pool;
    DbScriptConfig script;
};

} // namespace engine
