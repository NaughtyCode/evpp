# Changelog: Client Config Framework

## [Unreleased]

### Added
- (pending) `RenderConfig` struct (backend, resolution, fullscreen, vsync, msaa, hdr, max_fps, dynamic_resolution)
- (pending) `WindowConfig` struct (title, width, height, resizable, borderless, monitor)
- (pending) `InputConfig` struct (keybindings, mouse_sensitivity, invert_y, gamepad_deadzone, touch)
- (pending) `AudioConfig` struct (backend, sample_rate, channels, volumes, spatial_audio, mute_unfocused)
- (pending) `NetworkClientConfig` struct (server_address, port, reconnect, timeout, prediction, interpolation)
- (pending) `AssetConfig` struct (root_path, streaming_budget, lod_bias, texture_quality, preload_list)
- (pending) `UIConfig` struct (font_path, font_size, scale, locale, theme, color_blind_mode)
- (pending) `PlatformConfig` struct (save_data_path, cache_path, locale)
- (pending) 3-layer config loading: defaults → factory JSON → user settings JSON
- (pending) `SaveClientLayer3()` user config persistence
- (pending) `GetUserDataPath()` platform-aware path resolution
- (pending) User config corruption recovery (fallback to factory)

### Changed
- (pending) `ClientConfig`: expanded from 1 field to 9 config categories
- (pending) Documented RuntimeConfig vs ClientConfig boundary rules

### Fixed
- (pending) P0-9: ClientConfig is no longer a placeholder
- (pending) P1-18: Layered config loading for client
- (pending) P1-19: Clear RuntimeConfig/ClientConfig boundary
- (pending) P2-21: Graphics quality presets supported
- (pending) P2-22: Platform-aware paths for hardware detection
- (pending) P2-23: Network client config (server address, reconnect, prediction)
- (pending) P3-21: Platform-aware default paths
- (pending) P3-22: First-run experience config
- (pending) P3-23: Telemetry/privacy compliance config
- (pending) P3-24: Client reload callback with change details
- (pending) P3-25: Build-target-specific defaults
