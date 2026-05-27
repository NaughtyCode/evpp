# P1-2: Config Hot-Reload Notification Mechanism

## Objective

Implement a callback-based notification system so that when `ConfigManager::Reload()` is called, all subsystems that depend on configuration values receive a notification and can re-read their settings without a full process restart.

## Current State

`ConfigManager::Reload()` (`config.cc`) atomically replaces the configuration object, but **no subsystem is notified**:

```cpp
/* config.cc — Reload replaces config, but no callbacks fire */
void ConfigManager::Reload() {
    auto new_config = ParseConfigFile(config_path_);
    config_.store(std::move(new_config));  /* atomic swap */
    /* Nobody knows this happened */
}
```

Subsystems that read config at init time (log level, timer intervals, DB connection pool size, etc.) never see the updated values until process restart. This makes hot-reload effectively useless for runtime configuration changes.

## Root Cause

No observer/callback pattern exists in the config system. The design assumed config is read once at startup and never changes at runtime.

## Impact

- Log level cannot be changed at runtime for debugging
- Timer intervals cannot be tuned without restart
- DB connection pool size cannot be adjusted under load
- Network buffer limits cannot be relaxed during traffic spikes

## Implementation Steps

### Step 1: Define Reload Notification Interface

**File**: `src/runtime/config/config_manager.h`

```cpp
/* Callback type for config reload notifications */
using ConfigReloadCallback = std::function<void()>;

class ConfigManager {
public:
    /*
     * Register a callback to be invoked after each successful Reload().
     * Returns a registration ID for later unregistration.
     * Callbacks are invoked in registration order.
     * Callbacks MUST NOT throw — exceptions are caught and logged.
     */
    int RegisterReloadCallback(ConfigReloadCallback callback);

    /*
     * Unregister a previously registered callback by ID.
     */
    void UnregisterReloadCallback(int id);

    /*
     * Reload configuration and notify all registered callbacks.
     */
    bool Reload();

private:
    std::shared_mutex callbacks_mutex_;
    std::vector<std::pair<int, ConfigReloadCallback>> callbacks_;
    int next_callback_id_ = 1;
    /* ... existing members ... */
};
```

### Step 2: Implement Callback Invocation

**File**: `src/runtime/config/config_manager.cc`

```cpp
bool ConfigManager::Reload() {
    auto new_config = ParseConfigFile(config_path_);
    if (!new_config) {
        ENGINE_LOG_ERROR("Config reload failed: parse error");
        return false;
    }

    config_.store(std::move(new_config));
    ENGINE_LOG_INFO("Config reloaded successfully");

    /* Notify subscribers — under shared lock so new subscribers can
     * be added during callback execution but with defined ordering */
    std::shared_lock lock(callbacks_mutex_);
    for (auto& [id, callback] : callbacks_) {
        try {
            callback();
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("Config reload callback #{} failed: {}", id, e.what());
        }
    }

    return true;
}
```

### Step 3: Register Callbacks in Each Subsystem

Update each subsystem's initialization to register a reload callback:

**File**: `src/runtime/engine/engine.cc` (or individual subsystem files)

```cpp
/* In Engine::Init() or subsystem initialization: */

/* Log level */
ConfigManager::Instance().RegisterReloadCallback([]() {
    auto& config = ConfigManager::Instance().GetConfig();
    LogManager::SetLevel(config.log.level);
});

/* Timer */
ConfigManager::Instance().RegisterReloadCallback([]() {
    auto& config = ConfigManager::Instance().GetConfig();
    TimerManager::Instance().SetDefaultInterval(config.timer.default_interval_ms);
});

/* Network limits */
ConfigManager::Instance().RegisterReloadCallback([]() {
    auto& config = ConfigManager::Instance().GetConfig();
    NetworkManager::UpdateLimits(config.network.limits);
});

/* Database */
ConfigManager::Instance().RegisterReloadCallback([]() {
    auto& config = ConfigManager::Instance().GetConfig();
    DatabaseService::Instance().UpdatePoolSize(config.database.pool_size);
});
```

### Step 4: Add Lua API for Reload

**File**: `src/runtime/script/config_bind.cc`

```cpp
/* Expose to Lua: config.reload() */
int l_config_reload(lua_State* L) {
    bool ok = ConfigManager::Instance().Reload();
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

/* Expose to Lua: config.on_reload(callback) */
int l_config_on_reload(lua_State* L) {
    /* Store callback ref, register C++ callback that invokes it */
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ConfigManager::Instance().RegisterReloadCallback([L, ref]() {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ENGINE_LOG_ERROR("Lua config reload callback failed: {}",
                             lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    });
    return 0;
}
```

### Step 5: Tests

**File**: `src/tests/unit/config_reload_test.cc`

```cpp
TEST(ConfigReloadTest, CallbackInvoked_OnReload) {
    int call_count = 0;
    ConfigManager::Instance().RegisterReloadCallback([&]() { call_count++; });

    /* Reload a test config */
    ConfigManager::Instance().Reload();

    EXPECT_EQ(call_count, 1);
}

TEST(ConfigReloadTest, MultipleCallbacks_InOrder) {
    std::vector<int> order;
    ConfigManager::Instance().RegisterReloadCallback([&]() { order.push_back(1); });
    ConfigManager::Instance().RegisterReloadCallback([&]() { order.push_back(2); });

    ConfigManager::Instance().Reload();

    EXPECT_EQ(order, std::vector<int>({1, 2}));
}

TEST(ConfigReloadTest, CallbackException_DoesNotAffectOthers) {
    int call_count = 0;
    ConfigManager::Instance().RegisterReloadCallback([]() { throw std::runtime_error("boom"); });
    ConfigManager::Instance().RegisterReloadCallback([&]() { call_count++; });

    ConfigManager::Instance().Reload();

    EXPECT_EQ(call_count, 1);  /* second callback still invoked */
}

TEST(ConfigReloadTest, UnregisterCallback_NotInvoked) {
    int call_count = 0;
    int id = ConfigManager::Instance().RegisterReloadCallback([&]() { call_count++; });
    ConfigManager::Instance().UnregisterReloadCallback(id);

    ConfigManager::Instance().Reload();

    EXPECT_EQ(call_count, 0);
}
```

## Acceptance Criteria

1. `RegisterReloadCallback` and `UnregisterReloadCallback` are implemented
2. `Reload()` invokes all registered callbacks after config swap
3. Callback exceptions are caught and logged, not propagated
4. Subsystem callbacks are registered: log, timer, network, database
5. Lua API exposes `config.reload()` and `config.on_reload(callback)`
6. Tests verify callback invocation, ordering, exception isolation, and unregistration
7. Existing config tests continue to pass

## Dependencies

- None (independent of other P0/P1 plans)

## Estimated Effort

- ConfigManager changes: ~50 lines
- Subsystem callback registrations: ~30 lines × 4 subsystems = ~120 lines
- Lua binding: ~30 lines
- Tests: ~100 lines
- **Total**: ~300 lines

## Risks

- **Callback order dependency**: If subsystem A's callback depends on subsystem B's already having run, we need ordering guarantees. Mitigation: document that callbacks run in registration order; subsystems should not depend on other subsystems' reload ordering.
- **Thread safety**: `Reload()` might be called from any thread. The `shared_mutex` ensures thread-safe iteration. Callbacks should be designed to be thread-safe (most just re-read an atomic config value).
- **Circular dependency**: A callback that triggers another reload would cause infinite recursion. Mitigation: add a `reloading_` flag that prevents re-entrant Reload().
