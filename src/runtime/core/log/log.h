#pragma once

#include <string>

// MSVC 14.50+ (_MSC_VER >= 1950) has a broken std::snprintf in debug builds.
// Disable quill's debug assertion macro to avoid the compilation error.
#if defined(_MSC_VER) && _MSC_VER >= 1950 && !defined(QUILL_ASSERT_WITH_FMT)
#define QUILL_ASSERT_WITH_FMT(expr, fmt, ...) ((void) 0)
#endif

// ThreadContextManager.h forward-declares get_thread_name/get_thread_id as
// extern, but the definitions in ThreadUtilities.h are inline. On MSVC the
// extern declaration suppresses inline emission unless ThreadUtilities.h is
// included first, so the linker never finds the symbols. Include it early.
#if defined(_MSC_VER)
#include <quill/backend/ThreadUtilities.h>
#endif

#include <quill/Logger.h>

#include "runtime/core/engine_api.h"
#include "runtime/core/log/log_macros.h"

namespace engine {

struct LogConfig;

CLOUD_ENGINE_API quill::Logger* GetLogger(const std::string& name = "root");

CLOUD_ENGINE_API void InitLogger(const LogConfig& config);

CLOUD_ENGINE_API quill::Logger* CreateLogger(const LogConfig& config);

CLOUD_ENGINE_API void SetCurrentThreadName(const std::string& name);

CLOUD_ENGINE_API void ShutdownLogger();

}  // namespace engine
