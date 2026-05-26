#pragma once

#include <string>

namespace engine {

// ══════════════════════════════════════════════════════════════════════════════
// DbLogConfig — per-DBThread log configuration (R9)
// ══════════════════════════════════════════════════════════════════════════════
//
// Each DBThread creates an independent Quill logger during Start() via
// DBThread::CreateDbLogger(). The logger name is "db_vm_{N}" and log files
// are written to dir/db_vm_{N}_<timestamp>.log, isolated from main-thread
// and Physics log output.
//
// Mapped to engine::LogConfig inside CreateDbLogger() with:
//   logger_name         = "db_vm_" + index
//   log_filename        = "" (logger_name used as filename prefix)
//   rotation_frequency  = "" (size-based rotation only)
//   rotation_interval   = 1
//   rotation_time_daily = "00:00"

struct DbLogConfig {
    std::string dir = "logs/db_service";   // log output directory (separate from main logs)
    std::string level = "info";            // trace / debug / info / warn / error / fatal
    int rotation_size_mb = 100;            // roll to new file when this size is exceeded
    int max_backup_files = 10;             // keep at most this many old log files
};

// ══════════════════════════════════════════════════════════════════════════════
// DbScriptConfig — Lua script loading configuration (R12)
// ══════════════════════════════════════════════════════════════════════════════
//
// During EventLoop initialisation, the DBThread:
//   1. Sets import path to "db_scripts_dir;runtime_scripts_dir"
//      (db_scripts_dir is searched first — DB-specific scripts can shadow runtime ones).
//   2. If auto_load: loads runtime_scripts_dir first, then db_scripts_dir
//      (db_scripts_dir runs second so it can override globals set by runtime scripts).
//   3. Calls InitScript().

struct DbScriptConfig {
    std::string runtime_scripts_dir = "resources/script/runtime";   // shared runtime scripts (loaded first)
    std::string db_scripts_dir = "resources/script/db_service";     // DB-service-specific scripts (loaded second)
    bool        auto_load = true;      // auto-load script directories on EventLoop start
};

// ══════════════════════════════════════════════════════════════════════════════
// DbThreadPoolConfig — thread pool sizing and SPSC queue capacities
// ══════════════════════════════════════════════════════════════════════════════
//
// Each DBThread has two moodycamel::ConcurrentQueue instances:
//   request_queue  (MT → DBT): gated by request_queue_size  (back-pressure)
//   response_queue (DBT → MT): gated by response_queue_size (oldest dropped)

struct DbThreadPoolConfig {
    int thread_count = 4;              // number of DBThreads (R7, default 4)
    int request_queue_size = 1024;     // max pending requests per thread
    int response_queue_size = 1024;    // max pending responses per thread
};

// ══════════════════════════════════════════════════════════════════════════════
// DbConnectionPoolConfig — MongoClientPool sizing and Pop timeout
// ══════════════════════════════════════════════════════════════════════════════
//
// max_pool_size must be >= thread_count, otherwise threads will block on
// pool_->Pop() and eventually time out. Recommended: thread_count * 2 for
// some headroom (pool re-balancing, transient spikes).
//
// wait_queue_timeout_ms: injected into MongoUri as "waitQueueTimeoutMS" before
// pool creation (caller responsibility — see DatabaseService::Initialize docs).
//   -1 / 0 = infinite wait (DANGEROUS: Stop() will hang if no client is free).
//   Positive value = Pop() returns nullptr after this many ms, thread exits.

struct DbConnectionPoolConfig {
    int max_pool_size = 16;            // MongoClientPool max clients (must be >= thread_count)
    int wait_queue_timeout_ms = 5000;  // pool Pop() timeout in ms (0 = infinite — avoid)
};

// ══════════════════════════════════════════════════════════════════════════════
// DbServiceConfig — top-level configuration aggregate
// ══════════════════════════════════════════════════════════════════════════════
//
// Loaded from resources/config/server/db_service.json via
// ConfigManager::LoadDbServiceConfigFromFile().

struct DbServiceConfig {
    DbLogConfig log;
    DbThreadPoolConfig thread_pool;
    DbConnectionPoolConfig connection_pool;
    DbScriptConfig script;
};

} // namespace engine
