#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_engine_bridge.h"

#include "runtime/physics/physics_system.h"

namespace engine {

PhysicsEngineBridge& PhysicsEngineBridge::Instance() {
    static PhysicsEngineBridge instance;
    return instance;
}

bool PhysicsEngineBridge::Initialize(const std::string& config_dir,
                                      const std::string& assets_path,
                                      const std::string& scripts_dir) {
    return PhysicsSystem::Instance().Initialize(config_dir, assets_path, scripts_dir);
}

bool PhysicsEngineBridge::Start() {
    return PhysicsSystem::Instance().Start();
}

void PhysicsEngineBridge::Tick(uint64_t frame_id, float delta_time) {
    PhysicsSystem::Instance().Tick(frame_id, delta_time);
}

std::optional<PhysicsFrameResult> PhysicsEngineBridge::FetchResult(
    uint64_t frame_id, int timeout_ms) {
    return PhysicsSystem::Instance().FetchResult(frame_id, timeout_ms);
}

void PhysicsEngineBridge::Shutdown() {
    PhysicsSystem::Instance().Shutdown();
}

bool PhysicsEngineBridge::IsRunning() const {
    return PhysicsSystem::Instance().IsRunning();
}

bool PhysicsEngineBridge::IsHealthy() const {
    return PhysicsSystem::Instance().IsHealthy();
}

ScriptVM* PhysicsEngineBridge::GetScriptVM() {
    return &PhysicsSystem::Instance().GetScriptVM();
}

float PhysicsEngineBridge::GetFixedDeltaTime() const {
    return PhysicsSystem::Instance().GetFixedDeltaTime();
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
