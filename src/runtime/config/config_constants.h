#pragma once

namespace engine::config {

// File-system paths

// Default config directory (relative to working dir).
inline constexpr const char* kConfigDir = "resources/config";

// Config file paths relative to the config directory.
inline constexpr const char* kRuntimeConfigFile = "/runtime/runtime.json";
inline constexpr const char* kClientConfigFile = "/client/client.json";
inline constexpr const char* kServerConfigFile = "/server/server.json";
inline constexpr const char* kRedisConfigFile = "/server/redis.json";

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

// Client config defaults

inline constexpr const char* kDefaultClientUserSettingsFile = "settings.json";

// RenderConfig defaults
inline constexpr const char* kDefaultRenderBackend = "opengl";
inline constexpr int kDefaultResolutionWidth = 1920;
inline constexpr int kDefaultResolutionHeight = 1080;
inline constexpr int kDefaultMsaaSamples = 4;
inline constexpr int kDefaultMaxFps = 60;

// WindowConfig defaults
inline constexpr const char* kDefaultWindowTitle = "CloudEngine";
inline constexpr int kDefaultWindowWidth = 1280;
inline constexpr int kDefaultWindowHeight = 720;

// AudioConfig defaults
inline constexpr const char* kDefaultAudioBackend = "openal";
inline constexpr int kDefaultSampleRate = 44100;
inline constexpr int kDefaultChannels = 2;

// NetworkClientConfig defaults
inline constexpr const char* kDefaultServerAddress = "127.0.0.1";
inline constexpr int kDefaultServerPort = 7777;
inline constexpr int kDefaultReconnectMaxRetries = 10;
inline constexpr int kDefaultReconnectBaseDelayMs = 500;
inline constexpr int kDefaultReconnectMaxDelayMs = 30000;
inline constexpr int kDefaultNetworkTimeoutMs = 5000;
inline constexpr int kDefaultInterpolationDelayMs = 100;

// AssetConfig defaults
inline constexpr const char* kDefaultAssetRootPath = "resources/assets";
inline constexpr int kDefaultStreamingBudgetMb = 512;

// UIConfig defaults
inline constexpr const char* kDefaultFontPath = "resources/assets/fonts/default.ttf";
inline constexpr int kDefaultFontSize = 14;

// PID file defaults

inline constexpr const char* kDefaultPidFile = "server.pid";

// Resource limit defaults (runtime-configurable; these are the initial values)

inline constexpr uint32_t kDefaultMaxMessageSize = 64 * 1024;          /* 64 KiB */
inline constexpr uint32_t kDefaultMaxBufferCapacity = 256 * 1024;      /* 256 KiB */
inline constexpr uint32_t kDefaultMaxHttpBodySize = 10 * 1024 * 1024;  /* 10 MiB */
inline constexpr uint32_t kDefaultMaxMsgpackDepth = 64;

}  // namespace engine::config
