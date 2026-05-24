#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>

#include "runtime/vm/vm.h"

namespace engine {

// forward declarations
class PhysicsSystem;
class PhysicsThread;
class PhysicsWorld;

//=============================================================================
// PhysicsCustomPtr — enum indices for VMCustomPtrStore in the physics VM.
//
// Each physics subsystem object is registered in the VM's custom-pointer
// array under a fixed slot.  Use VMCustomPtrStore::GetAs<T>(index) to
// retrieve a typed pointer, or the convenience accessors on PhysicsScriptVM.
//=============================================================================

enum PhysicsCustomPtr : int {
    kPhysPtrSystem   = 1,  // PhysicsSystem*
    kPhysPtrThread   = 2,  // PhysicsThread*
    kPhysPtrWorld    = 3,  // PhysicsWorld*
    kPhysPtrScriptVM = 4,  // PhysicsScriptVM*
};

//=============================================================================
// PhysicsScriptVM — physics-dedicated VM inheriting from ScriptVM
//
// Uses a separate "physics_vm" logger so all log output carries the
// [physics_vm] prefix via Quill's %(logger) format pattern, making it easy
// to distinguish physics-script logs from the main engine VM.
//
// Owns a VMCustomPtrStore interface to the underlying lua_State for
// registering and retrieving subsystem object pointers (PhysicsSystem,
// PhysicsThread, PhysicsWorld, itself).
//=============================================================================

class PhysicsScriptVM : public ScriptVM {
public:
    PhysicsScriptVM();
    ~PhysicsScriptVM() override;

    PhysicsScriptVM(const PhysicsScriptVM&) = delete;
    PhysicsScriptVM& operator=(const PhysicsScriptVM&) = delete;
    PhysicsScriptVM(PhysicsScriptVM&&) noexcept = default;
    PhysicsScriptVM& operator=(PhysicsScriptVM&&) noexcept = default;

    //=================================================================
    // Custom pointer store — physics subsystem object registry
    //
    // The generic custom-ptr API (SetCustomPtr, GetCustomPtr,
    // PushCustomPtr, etc.) is inherited from ScriptVM.  The methods
    // below are physics-specific convenience wrappers.
    //=================================================================

    // Register the 4 core subsystem object pointers and store this VM
    // itself at kPhysPtrScriptVM.  Reserves kPhysPtrScriptVM (4) slots
    // before writing, ensuring no reallocation occurs for the core set.
    //
    // Called once by PhysicsSystem::Initialize() after all objects exist.
    void RegisterSubsystemObjects(PhysicsSystem* sys,
                                  PhysicsThread* thread,
                                  PhysicsWorld* world);

    // ── Typed subsystem accessors ────────────────────────────────────

    // These retrieve the 4 core subsystem objects from the underlying
    // custom-pointer array.  Returns nullptr if the slot has not been
    // populated (i.e. RegisterSubsystemObjects was never called).
    PhysicsSystem*   GetPhysicsSystem() const;
    PhysicsThread*   GetPhysicsThread() const;
    PhysicsWorld*    GetPhysicsWorld() const;

    // Built-in convenience: verify all 4 core slots are populated.
    // Returns true if GetPhysicsSystem(), GetPhysicsThread(),
    // GetPhysicsWorld(), and GetCustomPtr(kPhysPtrScriptVM) are all
    // non-null.
    bool AreCoreSlotsValid() const;

private:
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
