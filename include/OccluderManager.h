#pragma once

#include <vector>

#include "CullingEngineFrameInfo.h"

namespace CullingEngine {

#ifdef CULLING_ENGINE_HIDE_RASTERIZER
class CullingEngineCore;
#else
class Rasterizer;
#endif

class OccluderInput {
 public:
  uint16_t potentialVisible : 1;
  uint16_t backfaceCull : 1;
  uint16_t IsRawMesh : 1;
  uint16_t IsValidRawMesh : 1;
  uint16_t IsRowMajorMat : 1;

  uint16_t priority = 0;

  unsigned int nVert = 0;
  unsigned int nIdx = 0;
  const float* modelWorld = nullptr;
  const float* inVtx = nullptr;
  const unsigned short* inIdx = nullptr;

 public:
  OccluderInput() { ResetForSubmission(); }

  void ResetForSubmission() {
    potentialVisible = true;
    backfaceCull = true;
    IsRawMesh = false;
    IsValidRawMesh = false;
    IsRowMajorMat = false;
    priority = 0;
    nVert = 0;
    nIdx = 0;
    modelWorld = nullptr;
    inVtx = nullptr;
    inIdx = nullptr;
  }
};

class OccluderManager {
 public:
  enum {
    kDefaultGroupCount = 2,
    kDefaultOccluderInputCountPerGroup = 128,
  };

 public:
  std::vector<OccluderInput*> mPool;
  std::vector<int> mFlushOccluders;
  OccluderManager() {
    mPool.reserve(kDefaultOccluderInputCountPerGroup * kDefaultGroupCount);

    for (int idx = 0; idx < kDefaultGroupCount; idx++) {
      OccluderInput* occs =
          new OccluderInput[kDefaultOccluderInputCountPerGroup];
      for (int i = 0; i < kDefaultOccluderInputCountPerGroup; i++) {
        mPool.push_back(&occs[i]);
      }
    }

    mLastFinalRendered = 0;
    mCurrentOccNum = 0;
  }

  OccluderInput* RequestOccluder() {
    if (mLastFinalRendered != 0) {
      mLastFinalRendered = 0;
      mFlushOccluders.clear();
    }
    OccluderInput* occ = nullptr;
    if (mCurrentOccNum < mPool.size()) {
      occ = mPool[mCurrentOccNum++];
    } else {
      OccluderInput* occs =
          new OccluderInput[kDefaultOccluderInputCountPerGroup];
      for (int i = 0; i < kDefaultOccluderInputCountPerGroup; i++) {
        mPool.push_back(&occs[i]);
      }

      occ = mPool[mCurrentOccNum++];
    }
    occ->ResetForSubmission();
    return occ;
  }

  size_t GetMemorySizeInBytes() {
    size_t memorySizeInBytes = 0;

    // self
    memorySizeInBytes += sizeof(OccluderManager);

    // OccluderInput
    memorySizeInBytes += mPool.size() * sizeof(OccluderInput);

    // OccluderInput' Pointers
    memorySizeInBytes += mPool.capacity() * sizeof(OccluderInput*);

    // FlushOccluders
    memorySizeInBytes += mFlushOccluders.capacity() * sizeof(int);

    return memorySizeInBytes;
  }

  ~OccluderManager() {
    for (int i = 0; i < mPool.size(); i += kDefaultOccluderInputCountPerGroup) {
      delete[] mPool[i];
    }
    mPool.clear();
  }

  bool LatestFrameInterleaveDraw = false;
  int mCurrentOccNum = 0;
  int mCurrentRasterizedNum = 0;
  int mLastFinalRendered = 0;
  void OnRenderEnd();
};

class OccluderAABB {
 public:
  const float* LastOccluderVertices = nullptr;
  __m128 OccluderMinExtent[2] = {};
};

class RapidRasterizer {
 public:
  RapidRasterizer();
  ~RapidRasterizer();

 public:
  // Start Rasterizer
  void FlushCachedOccluder(int endOccluderNum);

 public:
  void BatchQuery(const float* bbox, unsigned int nMesh, bool* results);

 public:
  void SetResolution(unsigned int width, unsigned int height);
  void OnRenderFinish();
  void ConfigCoherentMode(int interleaveRatio);
  void SetNearPlane(float nearPlane);
  void RecordFlushAction();
  void BeforeQueryTreatTrueAsCulled();

 public:
  void SetCCW(bool IsModelCCW);
  int GetCW();
  void ShowOccludeeInDepthmap(int value);
  void OnNewFrame(uint64_t frame, bool criticalFrame, bool isRotating);
  void SetRenderType(int renderType);

  // Submit
 public:
  bool SubmitRawOccluder(const float* vertices, const unsigned short* indices,
                         unsigned int nVert, unsigned int nIdx,
                         const float* localToWorld, bool bRowMajorLocalToWorld,
                         bool backfaceCull);

  void SubmitBakedOccluder(unsigned short* inVtx, const float* modelWorld,
                           bool bRowMajorLocalToWorld,
                           int* outRasterizeTrianglesNum);

 public:
  bool RasterizeOccluder(OccluderInput* occ);

 public:
  void UsePrevDepthData();
  void UsePrevFrameOccluders(int sameOccluderNum);

 private:
  __m128* CalculateAABB(unsigned int nVert, const float* inVtx);

 public:
  void EnablePriorityQueue(bool value);
  void SyncOccluderPVS(bool* value);
  void ConfigQueryChildData(uint16_t* value);

 public:
  bool DumpDepthMap(unsigned char* depthMap, CullingEngine::DumpImageMode mode);

 public:
  size_t GetMemorySizeInBytes();

 public:
  Rasterizer* m_instance = nullptr;

  int mLastBakedOccluderNum = 0;
  int mLastFullOccluderNum = 0;

  uint64_t mFrameNum = 0;

  int mCurrentOccluderIdx = 0;

  CullingEngine::Matrix4x4 mViewProjT;

  OccluderManager* mOccluderCenter = nullptr;

  // cache 4 AABB
  OccluderAABB* AABBCache = nullptr;  // [4];
  int AABBNextStoreIdx = 0;
  int mValidAABB = 0;
  int mInvalidRawMeshNum = 0;
  int mInvalidRawMeshGroup = 0;

  uint8_t mShowCulled : 1;
  uint8_t mInRenderingState : 1;
  uint8_t PrintNumberOfOccluderOnce : 1;
  uint8_t mBackFaceCullOffFirst : 1;
  uint8_t mSortByPriorityQueue : 1;
};

}  // namespace CullingEngine
