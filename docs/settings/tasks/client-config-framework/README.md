# Task 10: Client Config Framework

**Priority:** P0 — client integration blocker
**Status:** pending
**Dependencies:** Task 1 (core-validation-fix), Task 4 (runtime-environment-selection), Task 9 (config-manager-unification)

## Scope

Build out `ClientConfig` with full rendering/window/input/audio/network/assets/UI/platform config structs. Implement layered config loading (default → file → user override).

## Issues Addressed

| Issue | Description |
|-------|-------------|
| P0-9 | ClientConfig是占位符（1字段），客户端集成时无配置可用 |
| P1-18 | 客户端配置无分层加载机制 |
| P1-19 | 双端共享配置的边界模糊 |
| P2-21 | 客户端无画质预设系统 |
| P2-22 | 客户端无硬件检测与自适应性能配置 |
| P2-23 | 客户端网络连接配置严重不足 |
| P3-21 | 客户端无平台感知的默认路径 |
| P3-22 | 客户端无首次运行体验配置 |
| P3-23 | 客户端无遥测隐私合规配置 |
| P3-24 | 客户端热更回调无变更详情 |
| P3-25 | 双端共享字段默认值无构建目标区分 |

## Key Changes

1. Add full `ClientConfig` structs: `RenderConfig`, `WindowConfig`, `InputConfig`, `AudioConfig`, `NetworkClientConfig`, `AssetConfig`, `UIConfig`, `PlatformConfig`
2. Implement 3-layer config loading: C++ defaults → `client.json` → `<user_data>/settings.json`
3. Add user config persistence: `SaveUserSettings()` writes user layer to disk
4. Implement user config corruption recovery: fall back to layer 2 + warn
5. Add platform-aware default paths (Windows/macOS/Linux/iOS/Android)
6. Define clear `RuntimeConfig` vs `ClientConfig` boundary rules

## Affected Files

- `src/runtime/config/config.h` (ClientConfig struct expansion)
- `src/runtime/config/config.cc`
- `src/runtime/config/config_constants.h`
- `resources/config/client/client.json`
- New file: `src/runtime/config/platform_paths.h`

## Restart Instructions

- Task status tracked in this README (status field above)
- Current progress saved in plan.md
- To restart: read plan.md → check CHANGELOG.md for last completed step → resume from next step
