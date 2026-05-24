#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/core/log/log.h"

//=============================================================================
// Physics-specific log macros.
//
// Each macro takes an explicit quill::Logger* obtained from PhysicsThread
// (via GetLogger()), PhysicsWorld, or PhysicsScriptVM.  No global logger
// lookup is performed — the logger must be passed in.
//=============================================================================

#define PHYSICS_LOG_TRACE(logger, fmt, ...)    LOG_TRACE_L1(logger, fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_DEBUG(logger, fmt, ...)    LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_INFO(logger, fmt, ...)     LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_WARN(logger, fmt, ...)     LOG_WARNING(logger, fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_ERROR(logger, fmt, ...)    LOG_ERROR(logger, fmt, ##__VA_ARGS__)
#define PHYSICS_LOG_CRITICAL(logger, fmt, ...) LOG_CRITICAL(logger, fmt, ##__VA_ARGS__)

#endif // ENGINE_PHYSICS_ENABLED
