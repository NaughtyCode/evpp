#pragma once

namespace engine::config {

// File-system paths

// Default config directory (relative to working dir).
inline constexpr const char* kConfigDir = "resources/config";

// Config file paths relative to the config directory.
inline constexpr const char* kRuntimeConfigFile = "/runtime/runtime.json";
inline constexpr const char* kClientConfigFile = "/client/client.json";
inline constexpr const char* kServerConfigFile = "/server/server.json";

// Resource / script directory defaults

inline constexpr const char* kDefaultResourceDir = "resources";
inline constexpr const char* kDefaultRuntimeScriptsDir = "resources/script/runtime";
inline constexpr const char* kDefaultClientScriptsDir = "resources/script/client";
inline constexpr const char* kDefaultServerScriptsDir = "resources/script/server";

// Log system defaults

inline constexpr const char* kDefaultLogDir = "logs";
inline constexpr const char* kDefaultLogLevel = "info";
inline constexpr const char* kDefaultLoggerName = "root";
inline constexpr int kDefaultRotationSizeMb = 100;
inline constexpr int kDefaultMaxBackupFiles = 10;

// Frame timing defaults

inline constexpr int kDefaultTargetFps = 30;
inline constexpr int kDefaultIntervalMs = 33;
inline constexpr int kDefaultSlowThresholdMultiplier = 2;

// HTTP / MessagePack defaults

inline constexpr double kDefaultHttpTimeoutSec = 10.0;
inline constexpr int kDefaultMsgpackMaxNestingDepth = 16;
inline constexpr size_t kDefaultMsgpackMaxPayloadSize = 1024 * 1024;  // 1 MB

// Database service defaults

inline constexpr const char* kDbServiceConfigFile = "/server/db_service.json";

// Server operations defaults

inline constexpr int kDefaultShutdownTimeoutSec = 30;
inline constexpr int kDefaultConnectionDrainTimeoutSec = 5;
inline constexpr int kDefaultMaxConnections = 10000;

// TCP keepalive defaults (seconds). 0 = use OS default.

inline constexpr int kDefaultTcpKeepaliveIdleSec = 0;
inline constexpr int kDefaultTcpKeepaliveIntervalSec = 0;
inline constexpr int kDefaultTcpKeepaliveCount = 0;

// PID file defaults

inline constexpr const char* kDefaultPidFile = "server.pid";

// Resource limit defaults (runtime-configurable; these are the initial values)

inline constexpr uint32_t kDefaultMaxMessageSize = 64 * 1024;          /* 64 KiB */
inline constexpr uint32_t kDefaultMaxBufferCapacity = 256 * 1024;      /* 256 KiB */
inline constexpr uint32_t kDefaultMaxHttpBodySize = 10 * 1024 * 1024;  /* 10 MiB */
inline constexpr uint32_t kDefaultMaxMsgpackDepth = 64;

}  // namespace engine::config
