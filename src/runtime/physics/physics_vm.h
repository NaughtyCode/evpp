#pragma once

//==============================================================================
// PHYSICS_INTERNAL_ACCESS — internal header guard macro
//
// See physics_system.h for full documentation.
// Including this header without the macro will cause a compile-time #error.
//==============================================================================
#ifndef PHYSICS_INTERNAL_ACCESS
#error \
	"physics_vm.h is internal to the physics subsystem. \
Use physics_engine_bridge.h instead. \
If you are writing physics-internal code, #define PHYSICS_INTERNAL_ACCESS \
before including this header."
#endif

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>

#include "runtime/vm/vm.h"

namespace engine {

// forward declarations
class PhysicsSystem;
class PhysicsThread;
class PhysicsWorld;

//=============================================================================
// PhysicsCustomPtr — enum indices for VMCustomPtrStore in the physics VM
//
// Each physics subsystem object is registered in the VM's custom-pointer
// array under a fixed slot. Use VMCustomPtrStore::GetAs<T>(index) to
// retrieve a typed pointer, or the convenience accessors on PhysicsScriptVM.
//=============================================================================

enum PhysicsCustomPtr : int {
	kPhysPtrSystem = 1,	 // PhysicsSystem*
	kPhysPtrThread = 2,	 // PhysicsThread*
	kPhysPtrWorld = 3,	// PhysicsWorld*
	kPhysPtrScriptVM = 4,  // PhysicsScriptVM*
};

//=============================================================================
// PhysicsScriptVM — physics-dedicated VM inheriting from ScriptVM
//
// [Thread Model]
//
//   This VM runs on the physics thread (PT) and is a PT-exclusive resource.
//
//   Lifecycle and thread ownership:
//     - Creation:        MT in PhysicsSystem::Initialize()
//                        (PT not yet started, no concurrency issue)
//     - Script loading:  MT in Initialize() — DoDirectory(), InitScript()
//                        (PT still not started, safe)
//     - Binding reg:     MT in Initialize() — physics_bindings::Register()
//                        (PT still not started, safe)
//     - Coroutine drive: PT in PhysicsSystem::UpdateScript() — calls
//                        script_vm_->UpdateScript() (PT-exclusive, no race)
//     - Collision cb:    PT invokes on_physics_collision via Lua state
//                        (PT-exclusive, no race)
//     - Destruction:     MT in Shutdown() — joins PT first, then destroys VM
//                        (happens-before guarantee, safe)
//
//   Start() is the critical point: before Start(), MT has exclusive access;
//   after Start(), PT has exclusive access. Therefore all data access on
//   script_vm_ requires no locking.
//
//   Uses a separate "physics_vm" logger so all log output carries the
//   [physics_vm] prefix via Quill's %(logger) format pattern, making it
//   easy to distinguish physics-script logs from the main engine VM.
//
//   Owns a VMCustomPtrStore interface to the underlying lua_State for
//   registering and retrieving subsystem object pointers (PhysicsSystem,
//   PhysicsThread, PhysicsWorld, itself).
//=============================================================================

class PhysicsScriptVM : public ScriptVM {
	public:
	PhysicsScriptVM();
	~PhysicsScriptVM() override;

	PhysicsScriptVM(const PhysicsScriptVM&) = delete;
	PhysicsScriptVM& operator=(const PhysicsScriptVM&) = delete;
	PhysicsScriptVM(PhysicsScriptVM&&) noexcept = default;
	PhysicsScriptVM& operator=(PhysicsScriptVM&&) noexcept = default;

	// ── Custom pointer store — physics subsystem object registry ───────
	//
	// The generic custom-ptr API (SetCustomPtr, GetCustomPtr,
	// PushCustomPtr, etc.) is inherited from ScriptVM. The methods
	// below are physics-specific convenience wrappers.
	//
	// Registers the 4 core subsystem object pointers and stores this VM
	// itself at kPhysPtrScriptVM. Reserves kPhysPtrScriptVM (4) slots
	// before writing, preventing reallocation for the core set.
	//
	// Called once by PhysicsSystem::Initialize() (MT, one-shot, PT not
	// yet started).

	void RegisterSubsystemObjects(PhysicsSystem* sys, PhysicsThread* thread, PhysicsWorld* world);

	// ── Typed subsystem accessors (PT-exclusive, no locks) ────────────
	//
	// Retrieve the 4 core subsystem objects from the underlying
	// custom-pointer array. Returns nullptr if the slot has not been
	// populated (i.e. RegisterSubsystemObjects was never called).

	PhysicsSystem* GetPhysicsSystem() const;
	PhysicsThread* GetPhysicsThread() const;
	PhysicsWorld* GetPhysicsWorld() const;

	// Built-in convenience: verify all 4 core slots are populated.
	// Returns true iff GetPhysicsSystem(), GetPhysicsThread(),
	// GetPhysicsWorld(), and GetCustomPtr(kPhysPtrScriptVM) are all
	// non-null.
	bool AreCoreSlotsValid() const;

	private:
};

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
