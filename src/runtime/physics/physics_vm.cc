#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_vm.h"

#include "runtime/physics/physics_log.h"

namespace engine {

PhysicsScriptVM::PhysicsScriptVM() {
    PHYSICS_LOG_INFO("[PhysicsVM] created, Lua state ready");
}

PhysicsScriptVM::~PhysicsScriptVM() {
    PHYSICS_LOG_INFO("[PhysicsVM] destroyed");
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
