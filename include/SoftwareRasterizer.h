/*
** Code from
* https://github.com/rawrunprotected/rasterizer/blob/master/SoftwareRasterizer/SoftwareRasterizer.h
* under CC0 1.0 Universal (CC0 1.0) Public Domain Dedication.
*/

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

#include "CullingEngineFrameInfo.h"
#include "MathUtility.h"
#include "PlatformSIMD.h"

namespace CullingEngine {
class OccluderInput;
}

namespace CullingEngine {

struct OccluderRenderCache {
  bool NeedsClipping = false;
  bool FlipOccluderFace = false;

  __m128 mat[4] = {};

  __m128 c0 = {};
  __m128 c1 = {};
  __m128 NegativeC1 = {};

  void PrepareCache(__m128& edge0, __m128& edge1, __m128& edge2) {
    mat[0] = edge0;
    mat[1] = edge1;
    mat[2] = edge2;
  }
  __m128 FullMeshInvExtents = {};
  __m128 FullMeshMinusRefMinInvExtents = {};
};

class PrimitiveBoundaryClipCache {
 public:
  PrimitiveBoundaryClipCache() {
    this->CalculateMask();  // initialize the mask
  }

  inline uint64_t GetPixelAABBMask(uint32_t blockX, uint32_t blockY) {
    return MinXBoundary[blockX == MinBlockX] &
           MaxXBoundary[blockX == MaxBlockX] &
           MinYBoundary[blockY == MinBlockY] &
           MaxYBoundary[blockY == MaxBlockY];
  }

  void UpdatePixelAABBData(int primitiveIdx) {
    uint16_t* mPixelData =
        (uint16_t*)PrimitivePixelBounds +
        (static_cast<std::size_t>(primitiveIdx) << 1);  // mask content, 0~7

    MinXBoundary[1] = this->PixelMinXMask[mPixelData[0]];
    MaxXBoundary[1] = this->PixelMaxXMask[mPixelData[8]];
    MinYBoundary[1] = this->PixelMinYMask[mPixelData[16]];
    MaxYBoundary[1] = this->PixelMaxYMask[mPixelData[24]];

    MinBlockX = mPixelData[1];
    MaxBlockX = mPixelData[9];
    MinBlockY = mPixelData[17];
    MaxBlockY = mPixelData[25];
  }

  void CalculateMask();

 public:
  uint32_t MinBlockX;
  uint32_t MaxBlockX;
  uint32_t MinBlockY;
  uint32_t MaxBlockY;

  __m128i PrimitivePixelBounds[4];

 private:
  uint64_t MinYBoundary[2];
  uint64_t MaxYBoundary[2];
  uint64_t MinXBoundary[2];
  uint64_t MaxXBoundary[2];

 public:
  uint64_t PixelMinXMask[8];
  uint64_t PixelMinYMask[8];
  uint64_t PixelMaxXMask[8];
  uint64_t PixelMaxYMask[8];
};

struct InterleaveConfig {
  bool IsRotating = false;
  bool mRenderRight = true;
  bool CurrentFrameInterleaveDrawing = false;

  void ConfigRotating(bool value) {
    IsRotating = value;
    MaskIndex = (int)value;
    mBlock_XRightStart = mBlock_XRightStartAll[MaskIndex];
    mBlock_XLeftEnd = mBlock_XLeftEndAll[MaskIndex];
    mPixel_XRightStart = mPixel_XRightStartAll[MaskIndex];
    mPixel_XLeftEnd = mPixel_XLeftEndAll[MaskIndex];
  }
  unsigned int MaskIndex = 0;  // set to 1 when rotating for high accuracy
  unsigned int mOctuple = 0;

  // current frame data frame update when config rotation
  uint32_t mBlock_XRightStart = 0;
  uint32_t mBlock_XLeftEnd = 0;
  uint32_t mPixel_XRightStart = 0;
  uint32_t mPixel_XLeftEnd = 0;
  uint64_t InterleaveFrame = 2;

 private:
  uint32_t mBlock_XRightStartAll[2] = {};
  uint32_t mBlock_XLeftEndAll[2] = {};
  uint32_t mPixel_XRightStartAll[2] = {};
  uint32_t mPixel_XLeftEndAll[2] = {};

  void UpdateConfig(int keyIndex, int octuple, uint32_t totalBlockX) {
    int SkipRegion = (totalBlockX * (8 - octuple)) >> 3;
    // this->mBlock_XRightStartAll[keyIndex] = SkipRegion - 1; //no overlap
    // region
    this->mBlock_XRightStartAll[keyIndex] =
        SkipRegion;  // at least one block is overlapped
    this->mBlock_XLeftEndAll[keyIndex] = (totalBlockX - SkipRegion);

    this->mPixel_XRightStartAll[keyIndex] =
        (this->mBlock_XRightStartAll[keyIndex] << 3);
    this->mPixel_XLeftEndAll[keyIndex] =
        (this->mBlock_XLeftEndAll[keyIndex] << 3) | 7;
  }

 public:
  void Config(uint32_t m_blocksX, uint32_t m_blocksY) {
    UpdateConfig(0, this->mOctuple, m_blocksX);
    UpdateConfig(1, 6, m_blocksX);
    ConfigRotating(MaskIndex);
  }
};

class Rasterizer {
 public:
  static constexpr int DEBUG_DATA_SIZE = 256;  // used to store all the counters
  static constexpr uint16_t MIN_UPDATED_BLOCK_DEPTH2 = 2;

 public:
  Rasterizer();
  ~Rasterizer();

  void SetResolution(unsigned int width, unsigned int height);

  void UpdateFrustumCullPlane();

  void SetModelViewProjectionT(const CullingEngine::Matrix4x4& localToClip);

  void ConfigBeforeRasterization();

  template <bool bQueryOccluder, bool OccludeeWidth1024>
  bool QueryVisibility(const float* minmaxf,
                       QueryDebugStates* outErr = nullptr);

  void DoRasterize(CullingEngine::OccluderMesh& raw);

  template <bool bQueryOccluder, bool bOccludeeWidth1024>
  bool Query2D(uint32_t minX, uint32_t maxX, uint32_t minY, uint32_t maxY,
               uint16_t maxZ);

  // Currently unused
  bool ReadBackDepth(unsigned char* target, CullingEngine::DumpImageMode mode);

  template <bool bHasTreeData, bool OccludeeWidth1024>
  void BatchQueryWithTree(const float* bbox, unsigned int nMesh, bool* results,
                          QueryDebugStates* outErr = nullptr);

  template <bool OccludeeWidth1024>
  void BatchQuery(const float* bbox, unsigned int nMesh, bool* results);

  size_t GetMemorySizeInBytes() const;

  uint32_t mWidthIn1024 : 1;
  uint32_t InterleaveRendering : 1;

  uint32_t ShowOccludeeInDepthMap : 1;
  uint32_t ShowOccludeeInDepthMapNext
      : 1;  // introduce this to enforce that ShowOccludeeInDepthMap could be
            // changed only at beginning of the frame
  uint32_t mDebugRenderMode : 1;

  // config vertices of model is stored by clockwise or counter-clockwise
  uint32_t mClockWise : 1;

  uint32_t mDebugRenderType
      : 8;  // 0 means default(No point, line), 1 means point, 2 means lines

  uint32_t mCurrValidOccluderNum = 0;
  uint32_t mLastOccluderNum = 0;
  uint64_t mGlobalFrameNum = 0;

  void ConfigGlobalFrameNum(uint64_t frameNum);

  float mNearPlane = 1.0f;

 private:
  template <bool bPixelAABBClippingQuad>
  void DrawQuad(__m128* x, __m128* y, __m128* invW, __m128* W,
                __m128 primitiveValid, __m128* edgeNormalsX,
                __m128* edgeNormalsY, __m128* areas);

  template <int RASTERIZE_CONFIG>
  void Rasterize(CullingEngine::OccluderMesh& raw);

  inline void PrecomputeRasterizationTable();

  uint64_t mCheckerBoardQueryMask[36];
  uint8_t mCheckerBoardQueryOffset[8];
  __m128 xFactors[2];

  __m128 m_localToClip[4];
  __m128 m_FrustumPlane[6];

  std::vector<uint64_t> m_precomputedRasterTables;
  uint64_t* m_pMaskTable = nullptr;
  // std::vector<uint16_t> m_hiZ; //m_hiz is not reuse second half of
  // m_precomputedRasterTables

  std::vector<__m128i> m_depthBuffer;
  uint64_t* m_pDepthBuffer = nullptr;

  uint16_t* m_pHiz = nullptr;
  uint16_t* m_pHizMax = nullptr;

  // for max data when value == 0, means all cells are 0, for quick initial
  // update for interleave mode, used by coherent mode to storePixel previous
  // cache

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  uint32_t m_blocksX = 0;
  uint32_t m_blocksY = 0;
  int mBlockWidthMin;
  uint32_t mBlockWidthMax;

  uint32_t m_blocksYMinusOne = 0;
  uint32_t m_blocksXFullDataRows = 0;

  // temp cache to avoid convert between int & float
  __m128i m_MaxCoord_WHWH;
  __m128i m_MaxCoordOccludee_WHWH;

  __m128i m_MaxCoord_WHWHOccluder;
  __m128i m_MinCoord_WHWHOccluder;

  PrimitiveBoundaryClipCache* mPrimitiveBoundaryClip = nullptr;

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
  // updateBlockWithMaxZ
  inline void UpdateBlockWithMaxZ(__m128 rowDepthLeft, __m128 rowDepthRight,
                                  uint64_t blockMask, __m128 depthRowDelta,
                                  __m128i primitiveMaxZV, __m128i* out,
                                  uint16_t* pBlockRowHiZ);

  inline void UpdateBlock(__m128i* depthRows, uint64_t blockMask, __m128i* out,
                          uint16_t* pBlockRowHiZ);
#endif

  inline void UpdateBlockMSCBPartial(uint32_t* depth32, uint64_t blockMask,
                                     __m128i* out, uint64_t* maskData,
                                     uint16_t* pBlockRowHiZ,
                                     uint16_t maxBlockDepth);

  // drawTriangle
  template <bool possiblyNearClipped, bool bBackFaceCull>
  void DrawTriangle(__m128* x, __m128* y, __m128* invW, __m128* W,
                    __m128 primitiveValid);

  template <bool possiblyNearClipped, bool bBackFaceCulling>
  void SplitToTwoTriangles(__m128* X, __m128* Y, __m128* W, __m128* invW,
                           __m128 primitiveValid, int validMask);

 public:
  void OnOccluderRenderFinish();

  // temp variables for occluder after query before rasterizer
  OccluderRenderCache mOccluderCache;

  inline bool InFrustum(__m128& boundsMin, __m128& boundsMax, __m128& extents);
  InterleaveConfig mInterleave;
  uint32_t m_blockSize = 0;
  uint32_t m_HizBufferSize = 0;

  uint32_t m_totalPixels = 0;

  void ConfigCoherent();

  uint16_t mCurrentOccludeeDepth = 0;

  uint64_t mCurrentOccludee = 0;
  std::vector<uint64_t> mOccludeeResults;

  uint32_t* DebugData = nullptr;  // Debug counters

  //__m128 mBatchMinZ;
  std::vector<uint16_t> m_depthBufferPointLines;

  uint64_t mIndexBuffer[4];

  // use 1~15 only, used to map 1 bit alive mask to 2 bit alive mask
  // where the 2 bit value stand for the index
  uint8_t mAliveIdxMask[16];

  std::vector<uint64_t> mAnyDataBlockMask;

 private:
  template <int PrimitveEdgeNum>
  void HandleDrawMode(__m128* x, __m128* y, __m128* z, uint32_t alivePrimitive);

  void DrawPixel(int x, int y, float zf);
  void DrawPixelSafe(int x, int y, float zf);
  void DrawLine(float* p, float* q);
#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
  void DumpColumnBlock(uint64_t t0, uint64_t t1, uint64_t t2, uint16_t maxDepth,
                       uint64_t blockMask);
#endif

 public:
  uint32_t mUpdateAnyBlock : 1;
  uint32_t mOccludeeTrueAsCulled : 1;
  uint16_t* mOccludeeTreeData = nullptr;

  const __m128* pLocalToClipRow = nullptr;

 public:
  uint32_t mIsNeedCheckInFrustum : 1;

 public:
  inline void InitVars() {
    mDebugRenderType = 0;

    mWidthIn1024 = true;
    InterleaveRendering = false;

    ShowOccludeeInDepthMap = true;
    ShowOccludeeInDepthMapNext = false;

    mDebugRenderMode = false;

    mClockWise = false;

    mUpdateAnyBlock = false;
    mOccludeeTrueAsCulled = false;

    mIsNeedCheckInFrustum = true;
  }

  inline void SetIsNeedCheckInFrustum(bool bIsNeedCheckInFrustum) {
    mIsNeedCheckInFrustum = bIsNeedCheckInFrustum;
  }

  inline bool GetIsNeedCheckInFrustum() { return mIsNeedCheckInFrustum; }

  // Debug
 public:
  void DumpAll(const float* bBBoxs, bool* results, unsigned int count);

  void DumpRasterizerStates();

  void DumpForInFrustum(__m128& boundsMin, __m128& boundsMax, __m128& extents);

  void DumpRasterizeOccluderInfo(CullingEngine::Matrix4x4& viewProj,
                                 CullingEngine::OccluderInput* ocInput,
                                 bool bQueryOccluder);

  void DumpBatchQueryResults(const float* bBBoxs, bool* results,
                             unsigned int count);

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG
 public:
  uint32_t m_DoRasterizerDebugData[kDoRasterizerCount];
  inline void ResetDoRasterizerDebugData() {
    for (int i = 0; i < kDoRasterizerCount; ++i) {
      m_DoRasterizerDebugData[i] = 0;
    }
  }

  void DumpDoRasterizerDebugData();
#endif
};

}  // namespace CullingEngine
