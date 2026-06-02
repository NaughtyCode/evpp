#pragma once

// Platform detection

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifndef ENGINE_PLATFORM_WINDOWS
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#define ENGINE_PLATFORM_WINDOWS 1
#else
#define ENGINE_PLATFORM_WINDOWS 0
#endif
#endif

#ifndef ENGINE_PLATFORM_ANDROID
#if defined(__ANDROID__)
#define ENGINE_PLATFORM_ANDROID 1
#else
#define ENGINE_PLATFORM_ANDROID 0
#endif
#endif

#ifndef ENGINE_PLATFORM_IOS
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define ENGINE_PLATFORM_IOS 1
#else
#define ENGINE_PLATFORM_IOS 0
#endif
#endif

#ifndef ENGINE_PLATFORM_MACOS
#if defined(__APPLE__) && !ENGINE_PLATFORM_IOS
#define ENGINE_PLATFORM_MACOS 1
#else
#define ENGINE_PLATFORM_MACOS 0
#endif
#endif

#ifndef ENGINE_PLATFORM_LINUX
#if defined(__linux__) && !ENGINE_PLATFORM_ANDROID
#define ENGINE_PLATFORM_LINUX 1
#else
#define ENGINE_PLATFORM_LINUX 0
#endif
#endif

#ifndef ENGINE_PLATFORM_APPLE
#define ENGINE_PLATFORM_APPLE (ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_IOS)
#endif

#ifndef ENGINE_PLATFORM_MOBILE
#define ENGINE_PLATFORM_MOBILE (ENGINE_PLATFORM_ANDROID || ENGINE_PLATFORM_IOS)
#endif

#ifndef ENGINE_PLATFORM_DESKTOP
#define ENGINE_PLATFORM_DESKTOP (ENGINE_PLATFORM_WINDOWS || ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_LINUX)
#endif

#ifndef ENGINE_PLATFORM_POSIX
#define ENGINE_PLATFORM_POSIX (ENGINE_PLATFORM_ANDROID || ENGINE_PLATFORM_IOS || ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_LINUX)
#endif

#ifndef ENGINE_PLATFORM_UNKNOWN
#if ENGINE_PLATFORM_WINDOWS || ENGINE_PLATFORM_ANDROID || ENGINE_PLATFORM_IOS || ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_LINUX
#define ENGINE_PLATFORM_UNKNOWN 0
#else
#define ENGINE_PLATFORM_UNKNOWN 1
#endif
#endif

#ifndef ENGINE_PLATFORM_NAME
#if ENGINE_PLATFORM_WINDOWS
#define ENGINE_PLATFORM_NAME "Windows"
#elif ENGINE_PLATFORM_ANDROID
#define ENGINE_PLATFORM_NAME "Android"
#elif ENGINE_PLATFORM_IOS
#define ENGINE_PLATFORM_NAME "iOS"
#elif ENGINE_PLATFORM_MACOS
#define ENGINE_PLATFORM_NAME "macOS"
#elif ENGINE_PLATFORM_LINUX
#define ENGINE_PLATFORM_NAME "Linux"
#else
#define ENGINE_PLATFORM_NAME "Unknown"
#endif
#endif

#ifndef ENGINE_FILE_WATCHER_ENABLED
#if ENGINE_PLATFORM_MOBILE
#define ENGINE_FILE_WATCHER_ENABLED 0
#else
#define ENGINE_FILE_WATCHER_ENABLED 1
#endif
#endif

#ifndef ENGINE_DATABASE_ENABLED
#if ENGINE_PLATFORM_MOBILE
#define ENGINE_DATABASE_ENABLED 0
#else
#define ENGINE_DATABASE_ENABLED 1
#endif
#endif

#if ENGINE_PLATFORM_WINDOWS
#ifndef H_OS_WINDOWS
#define H_OS_WINDOWS
#endif
#ifndef H_WINDOWS_API
#define H_WINDOWS_API
#endif
#endif

#if ENGINE_PLATFORM_APPLE
#ifndef H_OS_MACOSX
#define H_OS_MACOSX
#endif
#endif

#if ENGINE_PLATFORM_LINUX
#ifndef H_OS_LINUX
#define H_OS_LINUX
#endif
#endif

#if ENGINE_PLATFORM_ANDROID
#ifndef H_OS_ANDROID
#define H_OS_ANDROID
#endif
#endif

#if ENGINE_PLATFORM_IOS
#ifndef H_OS_IOS
#define H_OS_IOS
#endif
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
