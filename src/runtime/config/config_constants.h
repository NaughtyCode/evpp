#pragma once

namespace engine::config {

//============================================================================
// File-system paths
//============================================================================

// Default config directory (relative to working dir).
inline constexpr const char* kConfigDir = "resources/config";

// Config file paths relative to the config directory.
inline constexpr const char* kRuntimeConfigFile = "/runtime/runtime.json";
inline constexpr const char* kClientConfigFile = "/client/client.json";
inline constexpr const char* kServerConfigFile = "/server/server.json";

//============================================================================
// Resource / script directory defaults
//============================================================================

inline constexpr const char* kDefaultResourceDir = "resources";
inline constexpr const char* kDefaultRuntimeScriptsDir = "resources/script/runtime";
inline constexpr const char* kDefaultClientScriptsDir = "resources/script/client";
inline constexpr const char* kDefaultServerScriptsDir = "resources/script/server";

//============================================================================
// Log system defaults
//============================================================================

inline constexpr const char* kDefaultLogDir = "logs";
inline constexpr const char* kDefaultLogLevel = "info";
inline constexpr const char* kDefaultLoggerName = "root";
inline constexpr int kDefaultRotationSizeMb = 100;
inline constexpr int kDefaultMaxBackupFiles = 10;

//============================================================================
// Frame timing defaults
//============================================================================

inline constexpr int kDefaultTargetFps = 30;
inline constexpr int kDefaultIntervalMs = 33;
inline constexpr int kDefaultSlowThresholdMultiplier = 2;

//============================================================================
// HTTP / MessagePack defaults
//============================================================================

inline constexpr double kDefaultHttpTimeoutSec = 10.0;
inline constexpr int kDefaultMsgpackMaxNestingDepth = 16;
inline constexpr size_t kDefaultMsgpackMaxPayloadSize = 1024 * 1024;  // 1 MB

//============================================================================
// Database service defaults
//============================================================================

inline constexpr const char* kDbServiceConfigFile = "/server/db_service.json";

}  // namespace engine::config
