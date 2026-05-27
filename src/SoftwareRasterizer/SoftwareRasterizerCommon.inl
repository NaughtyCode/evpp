#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

#include "CullingEngineAPI.h"
#include "CullingEngineImageDump.h"
#include "CullingEngineLogger.h"
#include "MemoryUtility.h"
#include "SoftwareRasterizer.h"

#if defined(CULLING_ENGINE_NATIVE)
#pragma warning(disable : 4996)
#endif

#if defined(CULLING_ENGINE_PLATFORM_ANDROID)
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <fstream>
#endif

namespace CullingEngine {
enum RenderType {
  kRenderMesh = 0,
  kRenderMeshLine = 1,
  kRenderMeshPoint = 2,
  kRenderLine = 3,
  kRenderPoint = 4,
};

static const float maxInvW = std::sqrt(std::numeric_limits<float>::max());
#if defined(CULLING_ENGINE_PLATFORM_ANDROID) && !defined(__aarch64__)
static constexpr bool ARMV7 = true;
#else
static constexpr bool ARMV7 = false;
#endif

////if occludee clipped with near plane, do the frustum culling check
////otherwise, latest using quad could achieve frustum culling
static constexpr bool bFrustumCullIfClip = true;

static constexpr bool bDepthAtCenterOptimization =
    true;  // save one _mm_sub_ps for X

// Use branch to fast check whether occluder local to world transform having
// scales..
static constexpr bool bFastSetUpMatOp = true;

// 10% perf improvement for precomputeRasterizationTable
static constexpr bool bMaskTableOffsetOptimization = true;

static constexpr bool bQuadToTriangleMergeOp =
    true;  // in case only two quad active, could pack into one triangle patch

static constexpr bool bConvexOptimization = true;

static constexpr bool bOccludeeBitScanOp = 1;
static constexpr uint16_t bOccludeeMinDepthThreshold = 150 << 8;  // depth 9360

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
static constexpr bool bDumpTriangle = false;
#endif

static constexpr bool bPixelAABBClipping = true;

#ifdef CULLING_ENGINE_PLATFORM_WINDOWS
// static constexpr int DEBUG_DATA_SIZE = 256; //used to store all the counters
static constexpr bool PrintOccludeeState = true;
#endif

static bool DebugOccluderOccludee = true;
static constexpr bool bDebugOccluderOnly = 0;
static constexpr int bDebugOccluderPixelX = -1;
static constexpr int bDebugOccluderPixelY = -1;

static constexpr bool bDumpBlockColumnImage = false;
static constexpr int DebugDumpBlockX = -1 / 8;
static constexpr int DebugDumpBlockY = -1 / 8;

static constexpr uint16_t MIN_UPDATED_BLOCK_DEPTH = 1;
// static constexpr uint16_t MIN_UPDATED_BLOCK_DEPTH2 = 2;
#if defined(CULLING_ENGINE_PLATFORM_ANDROID) && !defined(__aarch64__)
static constexpr float MIN_PIXEL_DEPTH_FLOAT =
    1.17549435082e-38;  // ARMV7 minimal positive non denormalized number
static constexpr int MIN_PIXEL_DEPTH_FLOAT_INT =
    0x00400000;  // ARMV7 minimal positive non denormalized number
#else
static constexpr float MIN_PIXEL_DEPTH_FLOAT =
    3.44383110592e-41;  // the float value of  0x00006000;
static constexpr int MIN_PIXEL_DEPTH_FLOAT_INT =
    0x00006000;  // the float value of  0x00006000;
#endif

// static constexpr float MIN_PIXEL_DEPTH_FLOAT = 3.85374067974e-34;
// static constexpr int MIN_PIXEL_DEPTH_FLOAT_INT = 0x08001000;
static constexpr bool SupportDepthTill65K =
    false;  // 65k depth is working as expected now, however no obvious benefit

static constexpr int PureCheckerBoardApproach = 8;
static constexpr int CheckerBoardVizMaskApproach = 9;  // 2 x 4.5 _m128i
static constexpr int FullBlockApproach = 16;           // 2 x 8 rows
static constexpr int PairBlockNum = 9;  // change to 16 for full block approach
static constexpr bool VRS_X4Y4_Optimzation = true;

static constexpr bool OCCLUDEE_NEARCLIP_BBOX_CHECK_IGNORE_OPTIMIZATION = true;

static constexpr bool NEAR_CLIP_SURE_VISIBLE_OPTIMIZATION = true;
static constexpr int MAX_DEPTH = 0xFFFF;

// Effective Optimization configs
static constexpr bool CULL_FEATURE_HizPrimitiveCull = true;

static constexpr bool ReflectBoostBlockMask_Optimization =
    true;  // reduce the initial mask calculation time by HALF

static constexpr float floatCompressionBias =
    2.5237386e-29f;  // 0xFFFF << 12 reinterpreted as float
static constexpr float minEdgeOffset = -0.45f;

static constexpr int OFFSET_QUANTIZATION_BITS = 6;
static constexpr int OFFSET_QUANTIZATION_FACTOR = 1 << OFFSET_QUANTIZATION_BITS;

static constexpr float maxOffset = -minEdgeOffset;
// Remap [minOffset, maxOffset] to [0, OFFSET_QUANTIZATION]

// static constexpr float OFFSET_mul = (OFFSET_QUANTIZATION_FACTOR - 1) /
// (maxOffset - minEdgeOffset); //default static constexpr float OFFSET_add =
// 0.5f - minEdgeOffset * OFFSET_mul;

// scale and left shift one block. still can guarantee first all one and last
// all zero
static constexpr float OFFSET_mul = (OFFSET_QUANTIZATION_FACTOR - 1) * 64.4f /
                                    63.0f / (maxOffset - minEdgeOffset);
static constexpr float OFFSET_add = 0.5f - minEdgeOffset * OFFSET_mul - 1.0f;

static constexpr int SLOPE_QUANTIZATION_BITS = 6;
static constexpr int SLOPE_QUANTIZATION_FACTOR = 1 << SLOPE_QUANTIZATION_BITS;
}  // End of namespace CullingEngine

namespace CullingEngine {
/*
 * Initializes per-rasterizer lookup state that is independent of resolution.
 * The constructor prepares alive-lane decode tables, checkerboard query masks,
 * optional debug counters, and the boundary clip cache used by block traversal.
 */
Rasterizer::Rasterizer() {
  InitVars();

  mAliveIdxMask[0] = 0;
  mAliveIdxMask[1] = 0;             // 0001                                   0
  mAliveIdxMask[2] = 1;             // 0010                                   1
  mAliveIdxMask[3] = 1 << 2;        // 0011                              4
  mAliveIdxMask[4] = 2;             // 0100                                   2
  mAliveIdxMask[5] = 2 << 2;        // 0101                              8
  mAliveIdxMask[6] = (2 << 2) | 1;  // 0110                        9
  mAliveIdxMask[7] = (2 << 4) | (1 << 2);  // 0111                  36

  mAliveIdxMask[8] = 3;       // 1000                                    3
  mAliveIdxMask[9] = 3 << 2;  // 1001                               12
  mAliveIdxMask[10] = (3 << 2) | 1;             // 1010                      13
  mAliveIdxMask[11] = (3 << 4) | (1 << 2);      // 1011                 52
  mAliveIdxMask[12] = (3 << 2) | 2;             // 1100                      14
  mAliveIdxMask[13] = (3 << 4) | (2 << 2);      // 1101               56
  mAliveIdxMask[14] = (3 << 4) | (2 << 2) | 1;  // 1110           57
  mAliveIdxMask[15] = (3 << 6) | (2 << 4) | (1 << 2);  // 1111   228

  if (bDepthAtCenterOptimization == false) {
    xFactors[0] = _mm_setr_ps(0.375f, 0.875f, 0.375f, 0.875f);
    xFactors[1] = _mm_setr_ps(0, .5f, 00, 0.5f);
  } else {
    float one16 = 1.0f / 16;
    xFactors[0] =
        _mm_setr_ps(0.375f + one16, 0.875f + one16, 0.375f, 0.875f + one16);
    xFactors[1] = _mm_setr_ps(0 + one16, .5f + one16, 00 + one16, 0.5f + one16);
  }

  mPrimitiveBoundaryClip = new PrimitiveBoundaryClipCache();

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    if (DebugData == nullptr) {
      DebugData = new uint32_t[DEBUG_DATA_SIZE];
      memset(DebugData, 0, DEBUG_DATA_SIZE * sizeof(uint32_t));
    }
  });

  mCheckerBoardQueryOffset[0] = 0;
  mCheckerBoardQueryOffset[1] = mCheckerBoardQueryOffset[0] + 8;  // 8
  mCheckerBoardQueryOffset[2] = mCheckerBoardQueryOffset[1] + 7;  // 15
  mCheckerBoardQueryOffset[3] = mCheckerBoardQueryOffset[2] + 6;  // 21
  mCheckerBoardQueryOffset[4] = mCheckerBoardQueryOffset[3] + 5;  // 26
  mCheckerBoardQueryOffset[5] = mCheckerBoardQueryOffset[4] + 4;  // 30
  mCheckerBoardQueryOffset[6] = mCheckerBoardQueryOffset[5] + 3;  // 33
  mCheckerBoardQueryOffset[7] = mCheckerBoardQueryOffset[6] + 2;  // 35

  uint64_t checkerPattern =
      0xAA55AA55AA55AA55;  // white pattern,   refer to vertical black pixel
  uint8_t* checkerMask = (uint8_t*)&checkerPattern;
  int maskIdx = 0;
  for (uint8_t y0 = 0; y0 <= 7; y0++) {
    for (uint8_t y1 = y0; y1 <= 7; y1++) {
      mCheckerBoardQueryMask[maskIdx] = 0;
      uint8_t* mask8 = (uint8_t*)(mCheckerBoardQueryMask + maskIdx);
      maskIdx++;
      for (uint16_t y = y0; y <= y1; ++y) {
        mask8[y >> 1] |= checkerMask[y];
      }
    }
  }

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG
  ResetDoRasterizerDebugData();
#endif
}

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Converts two rows of premultiplied floating-point depth values into packed
 * unsigned 16-bit depth samples. Values are clamped against primitiveMaxZ and
 * the platform minimum depth so later HiZ comparisons never see denormals.
 */
static __m128i PackDepthPremultiplied(__m128 depthAf, __m128 depthBf,
                                      __m128i maxDepth) {
  __m128i depthA = _mm_castps_si128(depthAf);
  __m128i depthB = _mm_castps_si128(depthBf);
  depthA = _mm_min_epi32(depthA, maxDepth);
  depthB = _mm_min_epi32(depthB, maxDepth);
  __m128i minV = _mm_castps_si128(_mm_set1_ps(MIN_PIXEL_DEPTH_FLOAT));
  depthA = _mm_max_epi32(depthA, minV);
  depthB = _mm_max_epi32(depthB, minV);
  return _mm_packus_epi32(_mm_srai_epi32(depthA, 12),
                          _mm_srai_epi32(depthB, 12));
}
#endif

/*
 * Quantizes a vector of query max-Z values to the depth format stored in the
 * HiZ buffer. The return value keeps one 16-bit depth per lane for fast block
 * visibility tests.
 */
inline static __m128i PackQueryDepth(__m128 depthA) {
  if (SupportDepthTill65K) {
    __m128i d0 = _mm_castps_si128(depthA);
    __m128i d1 = _mm_slli_epi32(d0, 5);
    return _mm_srli_epi32(d1, 16);
    // return _mm_min_epi32(depthAi, _mm_set1_epi32(0xFFFF));
  }

  __m128i depthAi = _mm_srli_epi32(_mm_castps_si128(depthA), 12);
  return _mm_min_epi32(depthAi, _mm_set1_epi32(0xFFFF));
}

/*
 * Fast path used by the VRS 1x2 block writer. It duplicates packed 16-bit
 * depth values into both halves of each 32-bit lane so a pair of pixels can be
 * updated with one vector operation.
 */
inline static __m128i PackDepthPremultipliedVRS12Fast(__m128i depthA) {
  if (SupportDepthTill65K) {
    __m128i d160 = _mm_slli_epi32(depthA, 5);
    __m128i d16 = _mm_srli_epi32(d160, 16);
    __m128i d162 = _mm_slli_epi32(d16, 16);
    return _mm_or_si128(d16, d162);
  }

  __m128i d16 = _mm_srli_epi32(depthA, 12);
  __m128i d162 = _mm_slli_epi32(d16, 16);
  return _mm_or_si128(d16, d162);
}

/*
 * Packs a positive batch max-Z vector for conservative occludee and occluder
 * comparisons. The output is saturated to the supported 16-bit depth range.
 */
inline static __m128i PackPositiveBatchZ(__m128 maxZ) {
  if (SupportDepthTill65K) {
    __m128i maxZi = _mm_srai_epi32(_mm_castps_si128(maxZ), 11);
    return _mm_min_epu32(maxZi, _mm_set1_epi32(65535));

  } else {
    __m128i maxZi = _mm_srai_epi32(_mm_castps_si128(maxZ), 12);
    return _mm_min_epu32(maxZi, _mm_set1_epi32(65535));
  }
}

static std::mutex g_i_mutex;
static uint64_t* MaskTableCache = nullptr;
