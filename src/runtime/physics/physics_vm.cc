#ifdef ENGINE_PHYSICS_ENABLED

#define PHYSICS_INTERNAL_ACCESS
#include "runtime/physics/physics_vm.h"

#include "runtime/physics/physics_log.h"
#include "runtime/physics/physics_system.h"
#include "runtime/physics/physics_thread.h"
#include "runtime/physics/physics_world.h"

namespace engine {

// Construction / destruction

PhysicsScriptVM::PhysicsScriptVM() {
	ENGINE_LOG_INFO(GetLogger(), "[PhysicsVM] created, Lua state ready");
}

PhysicsScriptVM::~PhysicsScriptVM() {
	ENGINE_LOG_INFO(GetLogger(), "[PhysicsVM] destroyed");
}

// Custom pointer store — physics subsystem object registry

void PhysicsScriptVM::RegisterSubsystemObjects(PhysicsSystem* sys,
											   PhysicsThread* thread,
											   PhysicsWorld* world) {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));

	// Pre-allocate for the 4 core slots so no realloc occurs.
	store.Reserve(kPhysPtrScriptVM);

	store.Set(kPhysPtrSystem, sys);
	store.Set(kPhysPtrThread, thread);
	store.Set(kPhysPtrWorld, world);
	store.Set(kPhysPtrScriptVM, this);

	ENGINE_LOG_DEBUG(GetLogger(),
					 "[PhysicsVM] subsystem objects registered in custom ptr store "
					 "(count={}, cap={})",
					 store.Count(),
					 store.Capacity());
}

// ── Typed subsystem accessors ────────────────────────────────────────────

PhysicsSystem* PhysicsScriptVM::GetPhysicsSystem() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<PhysicsSystem>(kPhysPtrSystem);
}

PhysicsThread* PhysicsScriptVM::GetPhysicsThread() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<PhysicsThread>(kPhysPtrThread);
}

PhysicsWorld* PhysicsScriptVM::GetPhysicsWorld() const {
	VMCustomPtrStore store(const_cast<lua_State*>(GetState()));
	return store.GetAs<PhysicsWorld>(kPhysPtrWorld);
}

// ── Core slot validation ──────────────────────────────────────────────

bool PhysicsScriptVM::AreCoreSlotsValid() const {
	return GetPhysicsSystem() != nullptr && GetPhysicsThread() != nullptr &&
		   GetPhysicsWorld() != nullptr && GetCustomPtr(kPhysPtrScriptVM) != nullptr;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
