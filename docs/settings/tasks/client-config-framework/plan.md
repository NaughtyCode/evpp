# Implementation Plan: Client Config Framework

## Step 1: Define client config structs

Modify `config.h`:
- `RenderConfig`: backend (enum), resolution {width, height}, fullscreen (bool), vsync (bool), msaa_samples (int), hdr (bool), max_fps (int)
- `WindowConfig`: title (string), width/height (int), resizable (bool), borderless (bool), monitor (int)
- `InputConfig`: keybindings (map), mouse_sensitivity (float), mouse_invert_y (bool), gamepad_deadzone (float), touch_enabled (bool)
- `AudioConfig`: backend (enum), sample_rate (int), channels (int), master_volume (float), music_volume (float), sfx_volume (float), spatial_audio (bool), mute_when_unfocused (bool)
- `NetworkClientConfig`: server_address (string), server_port (int), reconnect_max_retries (int), reconnect_base_delay_ms (int), reconnect_max_delay_ms (int), timeout_ms (int), client_prediction (bool), interpolation_delay_ms (int)
- `AssetConfig`: root_path (string), streaming_budget_mb (int), lod_bias (float), texture_quality (enum), preload_list (vector<string>)
- `UIConfig`: font_path (string), font_size (int), scale (float), locale (string), theme (string), color_blind_mode (enum)
- `PlatformConfig`: save_data_path (string), cache_path (string), locale (string)

## Step 2: Integrate structs into ClientConfig

Modify `config.h` `ClientConfig`:
```cpp
struct ClientConfig {
    std::string scripts_dir = config::kDefaultClientScriptsDir;
    RenderConfig render;
    WindowConfig window;
    InputConfig input;
    AudioConfig audio;
    NetworkClientConfig network;
    AssetConfig assets;
    UIConfig ui;
    PlatformConfig platform;
};
```

## Step 3: Implement layered config loading

Modify `config.cc`:
- `LoadClientLayer1()`: populate C++ defaults
- `LoadClientLayer2(path)`: load `client.json` (factory settings), overlay on defaults
- `LoadClientLayer3(path)`: load `<user_data>/settings.json` (user settings), overlay on layer 2
- `SaveClientLayer3(path)`: persist user-modified fields to disk

## Step 4: Add platform-aware default paths

Create `src/runtime/config/platform_paths.h/cc`:
- `GetUserDataPath()`: platform-specific user data directory
  - Windows: `%APPDATA%/<app>/`
  - macOS: `~/Library/Application Support/<app>/`
  - Linux: `~/.local/share/<app>/`
  - iOS: `<Application_Home>/Documents/`
  - Android: `<app-internal-storage>/files/`

## Step 5: Implement user config corruption recovery

Modify `config.cc`:
- If `settings.json` parse fails → log warning → load layer 2 as safe fallback
- Offer `ResetUserSettings()` to delete corrupted file and restart fresh

## Step 6: Define RuntimeConfig/ClientConfig boundary

Document in code and README:
- Shared (`RuntimeConfig`): log, frame, resource_dir, sandbox_level, scripts_dir
- Server-only (`ServerConfig`): admin, mongodb, db_service, msgpack, http
- Client-only (`ClientConfig`): render, window, input, audio, network_client, assets, ui, platform

## Step 7: Update tests

In `test_config.cpp`:
- Test: all ClientConfig structs parse from JSON
- Test: 3-layer merge: default → factory → user (verify override precedence)
- Test: SaveClientLayer3() → LoadClientLayer3() round-trip
- Test: Corrupted settings.json → falls back to factory settings
- Test: Platform path resolution for each platform
