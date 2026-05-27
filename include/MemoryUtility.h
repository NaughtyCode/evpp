#pragma once

#include <stdlib.h>

#include <new>

#include "CullingEngineMacros.h"
#include "Vector3D.h"
#ifdef CULLING_ENGINE_PLATFORM_WINDOWS
#include <intrin.h>
#endif
#ifdef CULLING_ENGINE_PLATFORM_ANDROID
#include <byteswap.h>  //GCC Clang
#endif

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#endif

namespace CullingEngine {

inline uint64_t CullingEngine_bswap_64(uint64_t mask) {
#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
  return _byteswap_uint64(mask);  // bswap r64 is 2 uops on Intel CPUs
#elif defined(CULLING_ENGINE_PLATFORM_ANDROID)
  return bswap_64(mask);
#elif defined(CULLING_ENGINE_PLATFORM_LINUX)
  return __builtin_bswap64(mask);
#else
  return _OSSwapInt64(mask);
#endif
}

}  // namespace CullingEngine
