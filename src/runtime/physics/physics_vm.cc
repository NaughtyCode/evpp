#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_vm.h"

#include "runtime/physics/physics_log.h"

namespace engine {

PhysicsScriptVM::PhysicsScriptVM() {
    ENGINE_LOG_INFO(GetLogger(), "[PhysicsVM] created, Lua state ready");
}

PhysicsScriptVM::~PhysicsScriptVM() {
    ENGINE_LOG_INFO(GetLogger(), "[PhysicsVM] destroyed");
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
