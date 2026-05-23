#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

namespace engine {

class ScriptVM;

namespace physics_bindings {

// Register all physics Lua APIs into the given ScriptVM.
// Called by PhysicsSystem::Initialize() during VM setup.
void Register(ScriptVM& vm);

} // namespace physics_bindings
} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
