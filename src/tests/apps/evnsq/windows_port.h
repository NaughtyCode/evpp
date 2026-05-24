#pragma once

#include <evpp/platform_config.h>

// We must link against these libraries on windows platform for Visual Studio IDE
#ifdef _WIN32
#ifndef EVNSQ_EXPORTS
#pragma comment(lib, "evnsq_static.lib")
#endif
#endif

#define EVNSQ_EXPORT
