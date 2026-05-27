#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Platform and build feature macros
#if defined(_WIN32) || defined(_WIN64)
#define CULLING_ENGINE_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#define CULLING_ENGINE_PLATFORM_ANDROID 1
#elif defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define CULLING_ENGINE_PLATFORM_IOS 1
#elif defined(__APPLE__)
#define CULLING_ENGINE_PLATFORM_MACOS 1
#elif defined(__linux__)
#define CULLING_ENGINE_PLATFORM_LINUX 1
#endif

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS) || \
    defined(CULLING_ENGINE_PLATFORM_MACOS) ||   \
    defined(CULLING_ENGINE_PLATFORM_LINUX)
#define CULLING_ENGINE_NATIVE 1
#define CULLING_ENGINE_SUPPORT_ALL_FEATURES 1
#endif

#if defined(CULLING_ENGINE_PLATFORM_ANDROID) || defined(__aarch64__)
#define CULLING_ENGINE_ARM 1
#endif

// Intrinsic compatibility macro that mirrors the platform intrinsic naming
// style.
#if defined(CULLING_ENGINE_NATIVE) && !defined(__aarch64__) && \
    !defined(_MM_TRANSPOSE4_EPI32)
#define _MM_TRANSPOSE4_EPI32(row0, row1, row2, row3) \
  {                                                  \
    __m128i _Tmp3, _Tmp2, _Tmp1, _Tmp0;              \
    _Tmp0 = _mm_unpacklo_epi32((row0), (row1));      \
    _Tmp1 = _mm_unpacklo_epi32((row2), (row3));      \
    _Tmp2 = _mm_unpackhi_epi32((row0), (row1));      \
    _Tmp3 = _mm_unpackhi_epi32((row2), (row3));      \
    (row0) = _mm_unpacklo_epi64(_Tmp0, _Tmp1);       \
    (row1) = _mm_unpackhi_epi64(_Tmp0, _Tmp1);       \
    (row2) = _mm_unpacklo_epi64(_Tmp2, _Tmp3);       \
    (row3) = _mm_unpackhi_epi64(_Tmp2, _Tmp3);       \
  }
#endif

// Public API visibility
#if defined(CULLING_ENGINE_STATIC) || defined(CULLING_ENGINE_USE_INTERNAL)
#define CULLING_ENGINE_API
#elif defined(CULLING_ENGINE_LIB_EXPORT)
#if defined(__ANDROID__) || defined(__APPLE__)
#define CULLING_ENGINE_API __attribute__((__visibility__("default")))
#elif defined(_WIN64) || defined(_WIN32)
#define CULLING_ENGINE_API __declspec(dllexport)
#elif defined(__GNUC__)
#define CULLING_ENGINE_API __attribute__((visibility("default")))
#endif
#else
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
#if defined(__GNUC__)
#define CULLING_ENGINE_API __attribute__((dllimport))
#else
#define CULLING_ENGINE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#if __GNUC__ >= 4
#define CULLING_ENGINE_API __attribute__((visibility("default")))
#else
#define CULLING_ENGINE_API
#endif
#else
#error "Unsupported platform for CULLING_ENGINE_API"
#endif
#endif

// Public configuration IDs
#define CULLING_ENGINE_RENDER_MODE_FULL 0
#define CULLING_ENGINE_RENDER_MODE_COHERENT 1
#define CULLING_ENGINE_RENDER_MODE_COHERENT_FAST 2
#define CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE 12340

#define CULLING_ENGINE_SET_CCW 20

#define CULLING_ENGINE_GET_IS_SAME_CAMERA 99
#define CULLING_ENGINE_SET_USE_PREV_DEPTH_BUFFER 100
#define CULLING_ENGINE_RENDER_MODE 101
#define CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS 102

#define CULLING_ENGINE_GET_VERSION 212

#define CULLING_ENGINE_SET_COHERENT_MODE_SMALL_ROTATE_DOT_ANGLE_THRESHOLD 213
#define CULLING_ENGINE_SET_COHERENT_MODE_LARGE_ROTATE_DOT_ANGLE_THRESHOLD 214
#define CULLING_ENGINE_SET_COHERENT_MODE_CAMERA_DISTANCE_NEAR_THRESHOLD 215
#define CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT 220

#define CULLING_ENGINE_DESTROY 240

#define CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT 250
#define CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH 251
#define CULLING_ENGINE_GET_DEPTH_MAP 252
#define CULLING_ENGINE_SAVE_DEPTH_MAP 256
#define CULLING_ENGINE_SAVE_DEPTH_MAP_PATH 257
#define CULLING_ENGINE_SHOW_CULLED 260
#define CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP 261
#define CULLING_ENGINE_GET_MEMORY_USED 270

#define CULLING_ENGINE_SET_PRINT_LOG_IN_GAME 300
#define CULLING_ENGINE_GET_LOG 301
#define CULLING_ENGINE_PRINT_LOG 302

#define CULLING_ENGINE_CAPTURE_FRAME 400

#define CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE 599
#define CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST 600
#define CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER 602

#define CULLING_ENGINE_BAKE_MESH_SIMPLIFY_CONFIG 605
#define CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER 700
#define CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET 891
#define CULLING_ENGINE_BEFORE_QUERY_TREAT_TRUE_AS_CULLED 892
#define CULLING_ENGINE_SET_QUERY_TREE_DATA 893

#define CULLING_ENGINE_MIN_NEAR_PLANE 0.01f

// Debug feature switches
#define CULLING_ENGINE_ENABLE_ALL_DEBUG 0

#define CULLING_ENGINE_ENABLE_RASTERIZER_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_QUERY_VISIBILITY_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_BATCH_QUERY_DUMP \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_FLUSH_CACHED_OCCLUDER_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_RASTERIZE_OCCLUDER_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_MATRIX4X4_DEBUG \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)
#define CULLING_ENGINE_ENABLE_DUMP_RASTERIZER_STATES \
  (CULLING_ENGINE_ENABLE_ALL_DEBUG && 1)

#if defined(CULLING_ENGINE_NATIVE)
#define CULLING_ENGINE_ENABLE_OCCLUDER_OCCLUDEE_DEBUG 1
#else
#define CULLING_ENGINE_ENABLE_OCCLUDER_OCCLUDEE_DEBUG 0
#endif

#if CULLING_ENGINE_ENABLE_OCCLUDER_OCCLUDEE_DEBUG
#define CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(code) \
  do {                                                           \
    if (DebugOccluderOccludee) {                                 \
      code;                                                      \
    }                                                            \
  } while (false)
#else
#define CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(code) \
  do {                                                           \
  } while (false)
#endif

#if CULLING_ENGINE_ENABLE_QUERY_VISIBILITY_DEBUG
#define CULLING_ENGINE_MARK_QUERY_VISIBILITY_ERROR(       \
    debugStates, invisibleReason, visibleReason)          \
  do {                                                    \
    if ((debugStates) != nullptr) {                       \
      (debugStates)->InvisibleReason = (invisibleReason); \
      (debugStates)->VisibleReason = (visibleReason);     \
    }                                                     \
  } while (false)

#define CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(         \
    debugStates, prefix, usrData)                                       \
  do {                                                                  \
    if ((debugStates) != nullptr) {                                     \
      (debugStates)->DumpAndReset((prefix), static_cast<int>(usrData)); \
    }                                                                   \
  } while (false)

#define CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(bIsVisible, errType) \
  do {                                                                        \
    CULLING_ENGINE_MARK_QUERY_VISIBILITY_ERROR(                               \
        outErr, !(bIsVisible) ? (errType) : kQueryVisibilityNone,             \
        (bIsVisible) ? (errType) : kQueryVisibilityNone);                     \
  } while (false)
#else
#define CULLING_ENGINE_MARK_QUERY_VISIBILITY_ERROR( \
    debugStates, invisibleReason, visibleReason)    \
  do {                                              \
  } while (false)
#define CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS( \
    debugStates, prefix, usrData)                               \
  do {                                                          \
  } while (false)
#define CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(bIsVisible, errType) \
  do {                                                                        \
  } while (false)
#endif

// Logging convenience macros
#define CULLING_ENGINE_LOG_TRACE(...)                                     \
  ::CullingEngine::LogFormatted(::CullingEngine::Level::kTrace, __FILE__, \
                                __LINE__, __VA_ARGS__)
#define CULLING_ENGINE_LOG_DEBUG(...)                                     \
  ::CullingEngine::LogFormatted(::CullingEngine::Level::kDebug, __FILE__, \
                                __LINE__, __VA_ARGS__)
#define CULLING_ENGINE_LOG_INFO(...)                                      \
  ::CullingEngine::LogFormatted(::CullingEngine::Level::kInfo, __FILE__, \
                                __LINE__, __VA_ARGS__)
#define CULLING_ENGINE_LOG_WARNING(...)                                  \
  ::CullingEngine::LogFormatted(::CullingEngine::Level::kWarning, __FILE__, \
                                __LINE__, __VA_ARGS__)
#define CULLING_ENGINE_LOG_ERROR(...)                                     \
  ::CullingEngine::LogFormatted(::CullingEngine::Level::kError, __FILE__, \
                                __LINE__, __VA_ARGS__)
// Legacy short alias kept for external callers. Engine code should prefer the
// explicit severity macros above.
#define CULLING_ENGINE_LOGI(...) CULLING_ENGINE_LOG_INFO(__VA_ARGS__)

// Rasterizer debug helper macros
#define CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(var)                          \
  do {                                                                    \
    CULLING_ENGINE_LOG_DEBUG(#var " = {%f, %f, %f, %f}", arr[0], arr[1],  \
                             arr[2], arr[3]);                             \
  } while (false)

#define CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM_EXT(var, idx)                 \
  do {                                                                    \
    CULLING_ENGINE_LOG_DEBUG(#var "%d = {%f, %f, %f, %f}", idx, arr[0],  \
                             arr[1], arr[2], arr[3]);                     \
  } while (false)

#define CULLING_ENGINE_DUMP_DEBUG_ITEM(name)                              \
  do {                                                                    \
    if (DebugData[name] != 0) {                                           \
      CULLING_ENGINE_LOG_DEBUG(#name "(%d) = %d", name, DebugData[name]); \
    }                                                                     \
  } while (false)

#define CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(name)                 \
  do {                                                                   \
    if (DebugData[name] != 0) {                                          \
      CULLING_ENGINE_LOG_DEBUG("%s" #name "(%d) = %d", prefix, name,    \
                               DebugData[name]);                         \
    }                                                                    \
  } while (false)

#define CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(name)              \
  do {                                                            \
    if (m_DoRasterizerDebugData[name] != 0) {                     \
      CULLING_ENGINE_LOG_DEBUG(#name "(%d) = %d", name,          \
                               m_DoRasterizerDebugData[name]);    \
    }                                                             \
  } while (false)

#define CULLING_ENGINE_DUMP_BATCH_QUERY_WITH_RESULTS(idx)               \
  do {                                                                  \
    CULLING_ENGINE_LOG_DEBUG("%s%d<->%d", prefix, idx, results[idx]);   \
  } while (false)

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG
#define CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(key) \
  {                                                       \
    Rasterize<key>(raw);                                  \
    realKey = key;                                        \
    break;                                                \
  }
#else
#define CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(key) \
  {                                                       \
    Rasterize<key>(raw);                                  \
    break;                                                \
  }
#endif
