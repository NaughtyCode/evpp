#pragma once

// Platform detection

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#ifndef H_OS_WINDOWS
#define H_OS_WINDOWS
#endif
#ifndef H_WINDOWS_API
#define H_WINDOWS_API
#endif
#endif

#if defined(__APPLE__)
#define H_OS_MACOSX
#endif

#ifdef _DEBUG
#ifndef H_DEBUG_MODE
#define H_DEBUG_MODE
#endif
#endif

// Windows platform adapters

#ifdef H_OS_WINDOWS
#define usleep(us) Sleep((us) / 1000)
// snprintf and thread_local are provided by modern Windows SDK / MSVC;
// defining them as macros conflicts with the compiler and triggers error C1189.
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

// PRIu64 — platform-specific printf format specifier for uint64_t

#include <inttypes.h>
#ifndef PRIu64
#ifdef H_OS_WINDOWS
#define PRIu64 "I64u"
#else
#define PRIu64 "lu"
#endif
#endif

// Runtime API decoration fallback
// CLOUD_ENGINE_API is normally set by per-target CMake compile definitions:
//   empty for source/static integration targets (GameServer, GameClient, tests)
//   platform visibility attributes only for externally exported targets
// When not defined (e.g. test targets, IDE intellisense), default to empty.

#ifndef CLOUD_ENGINE_API
#define CLOUD_ENGINE_API
#endif
