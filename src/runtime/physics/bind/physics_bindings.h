#pragma once

// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// See physics_system.h for full documentation.
// Including this header without the macro will cause a compile-time #error.
#ifndef PHYSICS_INTERNAL_ACCESS
#error \
	"physics_bindings.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

namespace engine {

class ScriptVM;

namespace physics_bindings {

// Register all physics Lua APIs into the given ScriptVM.
// Called by PhysicsSystem::Initialize() during VM setup.
void Register(ScriptVM& vm);

}  // namespace physics_bindings
}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
