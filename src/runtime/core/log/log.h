#pragma once

#include <string>

// MSVC 14.50+ (_MSC_VER >= 1950) has a broken std::snprintf in debug builds.
// Disable quill's debug assertion macro to avoid the compilation error.
#if defined(_MSC_VER) && _MSC_VER >= 1950 && !defined(QUILL_ASSERT_WITH_FMT)
#define QUILL_ASSERT_WITH_FMT(expr, fmt, ...) ((void)0)
#endif

#include <quill/Logger.h>

#include "core/log/log_macros.h"
#include "engine_export.h"

namespace engine {

struct LogConfig;

ENGINE_API quill::Logger* GetLogger(const std::string& name = "root");

ENGINE_API void InitLogger(const LogConfig& config);

ENGINE_API quill::Logger* CreateLogger(const LogConfig& config);

ENGINE_API void ShutdownLogger();

} // namespace engine
