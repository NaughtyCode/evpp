#pragma once

#include <cstdlib>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine::platform {

// Returns the platform-specific user data directory for the application.
// On first run, the directory may not exist yet — callers should create it.
//
//   Windows : %APPDATA%/<app_name>/
//   macOS   : ~/Library/Application Support/<app_name>/
//   Linux   : ~/.local/share/<app_name>/
//   iOS     : <app sandbox>/Documents/  (set at runtime via config)
//   Android : <internal storage>/files/ (set at runtime via config)
//
// On unknown platforms, returns "./user_data/" as a safe fallback.
inline std::string GetUserDataPath(const std::string& app_name) {
#if ENGINE_PLATFORM_WINDOWS
    const char* appdata = std::getenv("APPDATA");
    if (appdata && appdata[0] != '\0') {
        return std::string(appdata) + "\\" + app_name + "\\";
    }
    // Fallback to USERPROFILE if APPDATA is not set.
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile && userprofile[0] != '\0') {
        return std::string(userprofile) + "\\AppData\\Roaming\\" + app_name + "\\";
    }
    return std::string(".\\user_data\\") + app_name + "\\";

#elif ENGINE_PLATFORM_IOS
    // iOS host apps should pass the sandbox path through config.
    return "./user_data/" + app_name + "/";

#elif ENGINE_PLATFORM_ANDROID
    // Android host apps should pass the internal files path through config.
    return "./user_data/" + app_name + "/";

#elif ENGINE_PLATFORM_MACOS
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/Library/Application Support/" + app_name + "/";
    }
    return "./user_data/" + app_name + "/";

#elif ENGINE_PLATFORM_LINUX
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.local/share/" + app_name + "/";
    }
    return "./user_data/" + app_name + "/";

#else
    // Unknown platform — safe fallback in the working directory.
    return "./user_data/" + app_name + "/";

#endif
}

// Returns the user settings file path for the application.
// This is <user_data>/settings.json — the Layer 3 user overrides file.
inline std::string GetUserSettingsPath(const std::string& app_name) {
    return GetUserDataPath(app_name) + "settings.json";
}

}  // namespace engine::platform
