# Changelog: Client Config Framework

## [2026-05-29]

### Added
- `RenderConfig` struct (backend, resolution, fullscreen, vsync, msaa, hdr, max_fps)
- `WindowConfig` struct (title, width, height, resizable, borderless, monitor)
- `InputConfig` struct (mouse_sensitivity, invert_y, gamepad_deadzone, touch)
- `AudioConfig` struct (backend, sample_rate, channels, volumes, spatial_audio, mute_unfocused)
- `NetworkClientConfig` struct (server_address, port, reconnect, timeout, prediction, interpolation)
- `AssetConfig` struct (root_path, streaming_budget, lod_bias, texture_quality, preload_list)
- `UIConfig` struct (font_path, font_size, scale, locale, theme, color_blind_mode)
- `PlatformConfig` struct (save_data_path, cache_path, locale)
- 3-layer config loading: defaults -> factory JSON -> user settings JSON
- `LoadClientUserSettings()` — loads user overrides from `<user_data>/settings.json`
- `SaveClientUserSettings()` — persists current ClientConfig to disk
- `LoadClientLayered()` — full layered load (Layer 1 -> 2 -> 3)
- `ResetClientUserSettings()` — deletes user settings and restores defaults
- `GetUserDataPath()` / `GetUserSettingsPath()` in `platform_paths.h` — platform-aware paths
- User config corruption recovery (rename corrupt file, fall back to factory)
- `first_run_completed` flag on ClientConfig for first-run experience detection
- Full client config validation: render backend, resolution, volumes, port ranges, enums
- Comprehensive unit tests for all new functionality

### Changed
- `ClientConfig`: expanded from 1 field (scripts_dir) to 9 config categories + first_run_completed
- `Load()` and `Reload()` now apply Layer 3 user overrides after Layer 2 factory config
- `ValidateClient()` extended with full field validation

### Fixed
- P0-9: ClientConfig is no longer a placeholder
- P1-18: Layered config loading for client
- P1-19: Clear RuntimeConfig/ClientConfig boundary documented in code
- P2-21: Graphics quality presets via texture_quality enum
- P2-22: Platform-aware paths for user data
- P2-23: Network client config (server address, reconnect, prediction)
- P3-21: Platform-aware default paths
- P3-22: First-run experience config (first_run_completed flag)
- P3-23: Telemetry/privacy compliance config basics
- P3-24: Client reload change tracking
- P3-25: Build-target-specific defaults
