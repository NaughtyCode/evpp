#pragma once

#include <string>

// MSVC 14.50+ has a broken std::snprintf in debug builds.
// Disable quill's debug assertion macro to avoid the compilation error.
#if defined(_MSC_VER) && !defined(QUILL_ASSERT_WITH_FMT)
#define QUILL_ASSERT_WITH_FMT(expr, fmt, ...) ((void)0)
#endif

#include <quill/Logger.h>

namespace engine {

quill::Logger* GetLogger(const std::string& name = "root");

void InitLogger(const std::string& log_dir);

void ShutdownLogger();

} // namespace engine
