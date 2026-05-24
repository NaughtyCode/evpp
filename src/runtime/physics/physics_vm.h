#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/vm/vm.h"

namespace engine {

//=============================================================================
// PhysicsScriptVM — physics-dedicated VM inheriting from ScriptVM
//
// Uses a separate "physics_vm" logger so all log output carries the
// [physics_vm] prefix via Quill's %(logger) format pattern, making it easy
// to distinguish physics-script logs from the main engine VM.
//=============================================================================

class PhysicsScriptVM : public ScriptVM {
public:
    PhysicsScriptVM();
    ~PhysicsScriptVM() override;

    PhysicsScriptVM(const PhysicsScriptVM&) = delete;
    PhysicsScriptVM& operator=(const PhysicsScriptVM&) = delete;
    PhysicsScriptVM(PhysicsScriptVM&&) noexcept = default;
    PhysicsScriptVM& operator=(PhysicsScriptVM&&) noexcept = default;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
