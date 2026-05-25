#pragma once

#include <cstdint>
#include <optional>
#include <string>

// This header is always includable, regardless of ENGINE_PHYSICS_ENABLED.
// When the macro is off, all methods are inline empty stubs.

#ifdef ENGINE_PHYSICS_ENABLED
// Full definitions available
#include "runtime/physics/physics_commands.h"
#endif

namespace engine {

class ScriptVM;

#ifdef ENGINE_PHYSICS_ENABLED
// (included above)
#else
// Stub type — ensures std::optional<PhysicsFrameResult> compiles
struct PhysicsFrameResult {
    uint64_t frame_id = 0;
    bool valid() const { return false; }
};
#endif // ENGINE_PHYSICS_ENABLED

//============================================================================
// PhysicsEngineBridge — single coupling point between Engine and physics system
//
// Engine never includes PhysicsSystem directly. It only includes this header
// and calls Bridge::Instance().Xxx().
//
// When ENGINE_PHYSICS_ENABLED is off:
//   - All methods compile to empty no-ops or return sentinel values.
//   - No physics code linked. No #ifdef required in Engine.
//============================================================================

class PhysicsEngineBridge {
public:
    PhysicsEngineBridge(const PhysicsEngineBridge&) = delete;
    PhysicsEngineBridge& operator=(const PhysicsEngineBridge&) = delete;

#ifdef ENGINE_PHYSICS_ENABLED

    // ── Full implementation (delegates to PhysicsSystem) ───────────────

    static PhysicsEngineBridge& Instance();

    bool Initialize(const std::string& config_dir,
                    const std::string& assets_path,
                    const std::string& scripts_dir);

    bool Start();
    void Tick(uint64_t frame_id, float delta_time);
    void UpdateScript();

    std::optional<PhysicsFrameResult> FetchResult(uint64_t frame_id,
                                                   int timeout_ms);

    void Shutdown();

    bool IsRunning() const;
    bool IsHealthy() const;
    ScriptVM* GetScriptVM();
    float GetFixedDeltaTime() const;

#else

    // ── Empty stubs (macro off — zero-cost no-ops) ────────────────────

    static PhysicsEngineBridge& Instance() {
        static PhysicsEngineBridge instance;
        return instance;
    }

    bool Initialize(const std::string&, const std::string&, const std::string&) {
        return false;
    }
    bool Start() { return false; }
    void Tick(uint64_t, float) {}
    void UpdateScript() {}
    std::optional<PhysicsFrameResult> FetchResult(uint64_t, int) {
        return std::nullopt;
    }
    void Shutdown() {}
    bool IsRunning() const { return false; }
    bool IsHealthy() const { return false; }
    ScriptVM* GetScriptVM() { return nullptr; }
    float GetFixedDeltaTime() const { return 0.01667f; }

#endif // ENGINE_PHYSICS_ENABLED

private:
    PhysicsEngineBridge() = default;
    ~PhysicsEngineBridge() = default;
};

} // namespace engine
