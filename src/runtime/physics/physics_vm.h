#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <quill/Logger.h>

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

    // Logger association — set by PhysicsSystem::Start() after the physics
    // thread creates its logger.
    void SetPhysicsLogger(quill::Logger* logger) { logger_ = logger; }
    quill::Logger* GetPhysicsLogger() const { return logger_; }

private:
    quill::Logger* logger_ = nullptr;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
