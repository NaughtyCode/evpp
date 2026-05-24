#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/core/log/log.h"

namespace engine {

// Physics-dedicated logger getter. Returns the "physics" logger created
// by PhysicsThread::CreatePhysicsLogger(). Safe to call from any thread
// after the physics logger has been created.
inline quill::Logger* GetPhysicsLogger() {
    return GetLogger("physics");
}

} // namespace engine

//=============================================================================
// Physics-specific log macros.
//
// These macros automatically use the physics-dedicated logger returned by
// GetPhysicsLogger(), so no logger argument is needed at the call site.
// All output carries the [physics] prefix via Quill's %(logger) pattern.
//=============================================================================

#define PHYSICS_LOG_TRACE(fmt, ...)    LOG_TRACE_L1(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_DEBUG(fmt, ...)    LOG_DEBUG(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_INFO(fmt, ...)     LOG_INFO(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_WARN(fmt, ...)     LOG_WARNING(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_ERROR(fmt, ...)    LOG_ERROR(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_CRITICAL(fmt, ...) LOG_CRITICAL(::engine::GetPhysicsLogger(), fmt, ##__VA_ARGS__)

#endif // ENGINE_PHYSICS_ENABLED
