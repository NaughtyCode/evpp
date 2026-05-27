#include "OccluderManager.h"

#include "CullingEngineAPI.h"
#include "CullingEngineLogger.h"
#include "MathUtility.h"
#include "SoftwareRasterizer.h"
#if defined(CULLING_ENGINE_NATIVE)
#include <string>
#endif
#include <fstream>

namespace CullingEngine {

// must be power of 2
static constexpr int Config_AABBCacheSize = 4;
static const int LargeTerrainOccluderPriority = 2;

RapidRasterizer::RapidRasterizer()
    : mShowCulled(false),
      mInRenderingState(false),
      PrintNumberOfOccluderOnce(false),
      mBackFaceCullOffFirst(true),
      mSortByPriorityQueue(false) {
  AABBNextStoreIdx = 0;
  AABBCache = new OccluderAABB[Config_AABBCacheSize];
  m_instance = new Rasterizer();
  mOccluderCenter = new OccluderManager();
}

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
#pragma warning(disable : 4996)
#endif

RapidRasterizer::~RapidRasterizer() {
  if (AABBCache != nullptr) delete[] AABBCache;
  AABBCache = nullptr;

  delete m_instance;
  m_instance = nullptr;

  delete mOccluderCenter;
  mOccluderCenter = nullptr;
#ifdef CULLING_ENGINE_DEBUG
  CULLING_ENGINE_LOG_DEBUG("RapidRasterizer released");
#endif
}

void RapidRasterizer::SetResolution(unsigned int width, unsigned int height) {
  m_instance->SetResolution(width, height);
}

bool RapidRasterizer::DumpDepthMap(unsigned char* depthMap,
                                   CullingEngine::DumpImageMode mode) {
  OnRenderFinish();
  if (!depthMap) {
    return false;
  }
  return m_instance->ReadBackDepth(depthMap, mode);
}

void RapidRasterizer::BatchQuery(const float* bbox, unsigned int nMesh,
                                 bool* results) {
  OnRenderFinish();

  m_instance->SetModelViewProjectionT(mViewProjT);
  m_instance->UpdateFrustumCullPlane();

  if (m_instance->mWidthIn1024) {
    m_instance->BatchQuery<true>(bbox, nMesh, results);
  } else {
    m_instance->BatchQuery<false>(bbox, nMesh, results);
  }

  if (mShowCulled) {
    for (unsigned int idx = 0; idx < nMesh; idx++) {
      results[idx] = !results[idx];
    }
  }
#if 0
	int visible = 0;
#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
	for (unsigned int idx = 0; idx < nMesh; idx++)
	{
		visible += results[idx];
		CULLING_ENGINE_LOG_DEBUG("Batch query debug: index=%u visibleCount=%d current=%d", idx, visible, (int)results[idx]);
	}
#else
	std::string path = "/sdcard/Android/data/com.qualcomm.CullingEnginedemo/files/";
	std::string queryOutput = path + "result.txt";
	std::ofstream stream(queryOutput.c_str(), std::ofstream::out);
	for (unsigned int idx = 0; idx < nMesh; idx++)
	{
		visible += results[idx];
		stream << "Idx " << idx << "Visible " << visible << "Current " << (int)results[idx] << "\n";
	}
	stream.close();
#endif
#endif
}

void RapidRasterizer::OnRenderFinish() {
  if (mInRenderingState) {
    for (int idx = 0; idx < mOccluderCenter->mFlushOccluders.size(); idx++) {
      int endNum = mOccluderCenter->mFlushOccluders[idx];
      if (endNum > mOccluderCenter->mCurrentRasterizedNum) {
        FlushCachedOccluder(endNum);
      }
    }
    FlushCachedOccluder(mOccluderCenter->mCurrentOccNum);
    mOccluderCenter->OnRenderEnd();
    m_instance->OnOccluderRenderFinish();
    mInRenderingState = false;
  }
}

void RapidRasterizer::OnNewFrame(uint64_t frame, bool criticalFrame,
                                 bool isRotating) {
  OnRenderFinish();  // force render finish

  if (PrintNumberOfOccluderOnce) {
    int occNum = this->mOccluderCenter->mLastFinalRendered;
    int bakedOcc = 0;
    int fullOcc = 0;
    for (int idx = 0; idx < occNum; idx++) {
      OccluderInput* occ = mOccluderCenter->mPool[idx];
      if (occ->inIdx == nullptr) {
        bakedOcc++;
      } else {
        fullOcc++;
      }
    }

    if (mLastBakedOccluderNum != bakedOcc || mLastFullOccluderNum != fullOcc) {
      CULLING_ENGINE_LOG_INFO(
          "Occluder count changed: baked %d -> %d, raw %d -> %d",
          mLastBakedOccluderNum, bakedOcc, mLastFullOccluderNum, fullOcc);
      mLastBakedOccluderNum = bakedOcc;
      mLastFullOccluderNum = fullOcc;
    }
  }
  if (mInvalidRawMeshNum > 0) {
    this->mInvalidRawMeshGroup += mInvalidRawMeshNum;
    if ((this->mFrameNum & 127) == 0) {
      CULLING_ENGINE_LOG_WARNING(
          "Invalid empty occluder meshes: current=%d submitted=%d "
          "last128=%d",
          this->mInvalidRawMeshNum, this->mOccluderCenter->mLastFinalRendered,
          this->mInvalidRawMeshGroup);
      this->mInvalidRawMeshGroup = 0;
    }

    mInvalidRawMeshNum = 0;
  }

  this->mFrameNum = frame;

  m_instance->mOccludeeTrueAsCulled = false;

  m_instance->ConfigGlobalFrameNum(this->mFrameNum);

  if (criticalFrame || m_instance->mDebugRenderType != 0) {
  } else {
    m_instance->mInterleave.ConfigRotating(isRotating);
  }

  m_instance->mInterleave.CurrentFrameInterleaveDrawing =
      !criticalFrame && this->m_instance->InterleaveRendering &&
      (this->mFrameNum > (CullingEngine::START_FRAME_COUNT + 1)) &&
      this->m_instance->mDebugRenderType == 0;

  this->mInRenderingState = true;

  if (mValidAABB > 0) {
    for (int i = 0; i < mValidAABB; i++) {
      AABBCache[i].LastOccluderVertices = nullptr;
    }
    mValidAABB = 0;
  }
}

void RapidRasterizer::SetNearPlane(float nearPlane) {
  if (nearPlane < CULLING_ENGINE_MIN_NEAR_PLANE) {
    nearPlane = CULLING_ENGINE_MIN_NEAR_PLANE;
  }

  m_instance->mNearPlane = nearPlane;
}

size_t RapidRasterizer::GetMemorySizeInBytes() {
  size_t memorySizeInBytes = 0;

  // self
  memorySizeInBytes += sizeof(RapidRasterizer);

  // Rasterizer
  memorySizeInBytes += m_instance->GetMemorySizeInBytes();

  // Occluder
  {
    if (this->mOccluderCenter != nullptr) {
      memorySizeInBytes += this->mOccluderCenter->GetMemorySizeInBytes();
    }
  }

  // OccluderAABB
  {
    if (AABBCache) {
      memorySizeInBytes += Config_AABBCacheSize * sizeof(OccluderAABB);
    }
  }

  return memorySizeInBytes;
}

void RapidRasterizer::ConfigCoherentMode(int octuple) {
  m_instance->mInterleave.mOctuple = octuple;
  m_instance->InterleaveRendering = octuple > 0;

  m_instance->ConfigCoherent();
}

void RapidRasterizer::SetCCW(bool IsModelCCW) {
  m_instance->mClockWise = IsModelCCW == false;
}

int RapidRasterizer::GetCW() { return m_instance->mClockWise; }

void RapidRasterizer::ShowOccludeeInDepthmap(int value) {
  m_instance->ShowOccludeeInDepthMapNext = value == 1;
  m_instance->ShowOccludeeInDepthMap = value == 1;
}

static bool NeedFlipFace(const float* modelWorld) {
  // return true;  //direct return true if no negative scale models
  float determinant =
      modelWorld[0] *
          (modelWorld[5] * modelWorld[10] - modelWorld[6] * modelWorld[9]) -
      modelWorld[1] *
          (modelWorld[4] * modelWorld[10] - modelWorld[6] * modelWorld[8]) +
      modelWorld[2] *
          (modelWorld[4] * modelWorld[9] - modelWorld[5] * modelWorld[8]);
  return determinant < 0;
}

static bool HasValidIndexRange(const unsigned short* indices,
                               unsigned int nVert, unsigned int nIdx) {
  if (indices == nullptr || nVert == 0 || nIdx == 0 || (nIdx % 3) != 0) {
    return false;
  }

  for (unsigned int idx = 0; idx < nIdx; ++idx) {
    if (indices[idx] >= nVert) {
      return false;
    }
  }

  return true;
}

bool RapidRasterizer::RasterizeOccluder(OccluderInput* occ) {
  m_instance->mUpdateAnyBlock = false;

  CullingEngine::Matrix4x4 LocalToWorldT;
  LocalToWorldT.UpdateTranspose(occ->modelWorld, occ->IsRowMajorMat);

  CullingEngine::Matrix4x4 LocalToClipT;
  CullingEngine::Matrix4x4::Multiply(mViewProjT, LocalToWorldT, LocalToClipT);
  m_instance->SetModelViewProjectionT(LocalToClipT);

#if CULLING_ENGINE_ENABLE_MATRIX4X4_DEBUG
  mViewProjT.Dump("worldToClipMatrix");
  LocalToWorldT.Dump("LocalToWorldMatrix");
  LocalToClipT.Dump("ModelViewProjMatrix");
#endif

  if (occ->IsRawMesh == false) {
    const float* minExtents = occ->inVtx + 2;  // minExtents
    bool visible = false;

#if CULLING_ENGINE_ENABLE_QUERY_VISIBILITY_DEBUG

#if CULLING_ENGINE_ENABLE_RASTERIZE_OCCLUDER_DEBUG
    this->m_instance->DumpRasterizeOccluderInfo(mViewProjT, occ, true);
#endif  // End of CULLING_ENGINE_ENABLE_RASTERIZE_OCCLUDER_DEBUG

    QueryDebugStates debugStatesLocal;
    QueryDebugStates* debugStates = &debugStatesLocal;

    visible = m_instance->QueryVisibility<true, false>(minExtents, debugStates);

    CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(
        debugStates, "RasterizeOccluder1", visible);

#else  // Else of CULLING_ENGINE_ENABLE_QUERY_VISIBILITY_DEBUG

    visible = m_instance->QueryVisibility<true, false>(minExtents);

#endif  // End of CULLING_ENGINE_ENABLE_QUERY_VISIBILITY_DEBUG

    if (visible) {
      m_instance->mOccluderCache.FlipOccluderFace =
          NeedFlipFace(occ->modelWorld);

      uint16_t* meta = (uint16_t*)occ->inVtx;

      CullingEngine::OccluderMesh raw;
      raw.EnableBackface = meta[0] & 1;
      raw.SuperCompress = meta[1] <= CullingEngine::SuperCompressVertNum;
      raw.QuadSafeBatchNum = meta[2];
      raw.TriangleBatchIdxNum = meta[3];

      // ignore first 8 float as they are stored 64bit meta data and 6float for
      // minExtent
      raw.Vertices = (float*)(occ->inVtx + 8);

      m_instance->DoRasterize(raw);
    }
  } else {
    if (occ->IsValidRawMesh == false) {
      this->mInvalidRawMeshNum++;
      return true;  // for invalid occluder, potential visible set as true
    }
    bool visible = false;

    __m128* OccluderMinExtent = CalculateAABB(occ->nVert, occ->inVtx);

    float minExtent[6];
    float* inputME = (float*)OccluderMinExtent;
    memcpy(minExtent, inputME, 3 * sizeof(float));
    memcpy(minExtent + 3, inputME + 4, 3 * sizeof(float));

    visible = m_instance->QueryVisibility<true, false>(minExtent);

    if (visible) {
      m_instance->mOccluderCache.FlipOccluderFace =
          NeedFlipFace(occ->modelWorld);

      __m128 scalingXYZW = _mm_setr_ps(1.0f, 1.0f, 1.0f, 0);
      __m128 InvExtents = _mm_div_ps(scalingXYZW, OccluderMinExtent[1]);
      // check whether any of BoundsRefinedExtents is zero
      __m128 positive = _mm_cmpgt_ps(OccluderMinExtent[1], _mm_setzero_ps());
      __m128 invExtents = _mm_and_ps(InvExtents, positive);

      __m128 minusRefMinInvExtents =
          _mm_mul_ps(_mm_negate_ps_soc(invExtents), OccluderMinExtent[0]);
      minusRefMinInvExtents =
          _mm_add_ps(minusRefMinInvExtents, _mm_setr_ps(0, 0, 0, 1));

      // temp set of rasterize required input
      m_instance->mOccluderCache.FullMeshInvExtents = invExtents;
      m_instance->mOccluderCache.FullMeshMinusRefMinInvExtents =
          minusRefMinInvExtents;

      CullingEngine::OccluderMesh raw;
      raw.Indices = occ->inIdx;
      raw.Vertices = occ->inVtx;
      raw.TriangleBatchIdxNum = occ->nIdx;
      raw.VerticesNum = occ->nVert;
      raw.EnableBackface = occ->backfaceCull;

      m_instance->DoRasterize(raw);
    }
  }
  return m_instance->mUpdateAnyBlock;
}

void RapidRasterizer::SubmitBakedOccluder(unsigned short* inVtx,
                                          const float* modelWorld,
                                          bool bRowMajorMat,
                                          int* outRasterizeTrianglesNum) {
  ////uint16_t* pVint = (uint16_t*)compressData;
  //////pVint += 3;
  ////pVint[0] = (int)enableBackfaceCull + ((int)quadData.IsTerrain << 1);
  ////pVint[1] = quadData.IsPlanar;
  ////pVint[2] = quadBatchNum;
  ////pVint[3] = triangleBatchNum;

  uint16_t* meta = inVtx;

  OccluderInput* occ = mOccluderCenter->RequestOccluder();
  occ->inVtx = (float*)inVtx;
  occ->inIdx = nullptr;
  occ->nVert = 0;
  occ->nIdx = 0;
  occ->modelWorld = modelWorld;
  occ->backfaceCull = meta[0] & 1;

  occ->priority = occ->backfaceCull;
  occ->IsRawMesh = false;
  occ->IsRowMajorMat = bRowMajorMat;

  if (meta[0] & 32)  // terrain at bit 5
  {
    occ->priority |= LargeTerrainOccluderPriority;
  }

  if (outRasterizeTrianglesNum) {
    uint32_t triangleBatchIdxNum = meta[3];
    if (triangleBatchIdxNum) {
      (*outRasterizeTrianglesNum) = (int)(triangleBatchIdxNum / 3);
    } else {
      uint32_t ouadSafeBatchNum = meta[2];
      (*outRasterizeTrianglesNum) = (int)(ouadSafeBatchNum * 2);
    }
  }
}

bool RapidRasterizer::SubmitRawOccluder(const float* inVtx,
                                        const unsigned short* inIdx,
                                        unsigned int nVert, unsigned int nIdx,
                                        const float* modelWorld,
                                        bool IsRowMajorMat, bool backfaceCull) {
  if (inVtx == nullptr || modelWorld == nullptr ||
      !HasValidIndexRange(inIdx, nVert, nIdx)) {
    return false;
  }

  OccluderInput* occ = mOccluderCenter->RequestOccluder();
  occ->inVtx = inVtx;
  occ->inIdx = inIdx;
  occ->nVert = nVert;
  occ->nIdx = nIdx;
  occ->modelWorld = modelWorld;
  occ->backfaceCull = backfaceCull;
  occ->priority = backfaceCull;
  occ->IsRawMesh = true;
  occ->IsValidRawMesh = true;
  occ->IsRowMajorMat = IsRowMajorMat;

  return occ->IsValidRawMesh;
}

void RapidRasterizer::UsePrevDepthData() {
  // use same frame as previous
  m_instance->mCurrValidOccluderNum = m_instance->mLastOccluderNum;
  mInRenderingState = false;
}

void RapidRasterizer::SetRenderType(int renderType) {
  if (renderType == -1) {
    // Keep this cycling order in sync with RenderType.
    this->m_instance->mDebugRenderType =
        (this->m_instance->mDebugRenderType + 1) % 5;
  } else {
    this->m_instance->mDebugRenderType = renderType;
  }

  if (m_instance->m_totalPixels != 0 &&
      this->m_instance->mDebugRenderType != 0) {
    m_instance->m_depthBufferPointLines.resize(m_instance->m_totalPixels);
  }

  this->m_instance->mDebugRenderMode = (this->m_instance->mDebugRenderType > 0);
  CULLING_ENGINE_LOG_INFO("Render debug mode set to %d",
                      this->m_instance->mDebugRenderType);
}

void RapidRasterizer::UsePrevFrameOccluders(int sameOccluderNum) {
  mOccluderCenter->mCurrentOccNum = sameOccluderNum;
  mOccluderCenter->mCurrentRasterizedNum = 0;
  mOccluderCenter->mLastFinalRendered = 0;
}

void RapidRasterizer::FlushCachedOccluder(int endOccluderNum) {
#if CULLING_ENGINE_ENABLE_FLUSH_CACHED_OCCLUDER_DEBUG
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: flush cached occluders");
#endif

  // Record the state of Latest frame interleave state for occluder PVS usage
  mOccluderCenter->LatestFrameInterleaveDraw =
      m_instance->mInterleave.CurrentFrameInterleaveDrawing;

  // already finish the rendering
  if (mOccluderCenter->mCurrentRasterizedNum >=
          mOccluderCenter->mCurrentOccNum ||
      mInRenderingState == false) {
    return;
  }

  endOccluderNum = std::min(endOccluderNum, mOccluderCenter->mCurrentOccNum);
  if (endOccluderNum <= mOccluderCenter->mCurrentRasterizedNum) {
    return;
  }

  // delay configBeforeRasterization to support same frame skip feature
  if (this->m_instance->mCurrValidOccluderNum == 0) {
    this->m_instance->ConfigBeforeRasterization();
    this->m_instance->mCurrValidOccluderNum++;
  }

  if (mSortByPriorityQueue == false) {
    for (int idx = mOccluderCenter->mCurrentRasterizedNum; idx < endOccluderNum;
         idx++) {
      OccluderInput* occ = mOccluderCenter->mPool[idx];
      occ->potentialVisible = RasterizeOccluder(occ);
    }
  } else {
    if (this->mBackFaceCullOffFirst) {
      for (int idx = mOccluderCenter->mCurrentRasterizedNum;
           idx < endOccluderNum; idx++) {
        OccluderInput* occ = mOccluderCenter->mPool[idx];
        if (occ->priority == 0) {
          occ->potentialVisible = RasterizeOccluder(occ);
        }
      }
      for (int idx = mOccluderCenter->mCurrentRasterizedNum;
           idx < endOccluderNum; idx++) {
        OccluderInput* occ = mOccluderCenter->mPool[idx];
        if (occ->priority == 1) {
          occ->potentialVisible = RasterizeOccluder(occ);
        }
      }
    } else {
      for (int idx = mOccluderCenter->mCurrentRasterizedNum;
           idx < endOccluderNum; idx++) {
        OccluderInput* occ = mOccluderCenter->mPool[idx];
        if (occ->priority < 2) {
          occ->potentialVisible = RasterizeOccluder(occ);
        }
      }
    }

    for (int idx = mOccluderCenter->mCurrentRasterizedNum; idx < endOccluderNum;
         idx++) {
      OccluderInput* occ = mOccluderCenter->mPool[idx];
      if (occ->priority >= LargeTerrainOccluderPriority) {
        occ->potentialVisible = RasterizeOccluder(occ);
      }
    }
  }

  mOccluderCenter->mCurrentRasterizedNum = endOccluderNum;
}

__m128* RapidRasterizer::CalculateAABB(unsigned int nVert,
                                       const float* vertices) {
  for (int idx = 0; idx < mValidAABB; idx++) {
    if (AABBCache[idx].LastOccluderVertices == vertices) {
      return AABBCache[idx].OccluderMinExtent;
    }

    // used to verify the effectiveness when replaying...
    if (false) {
      if (AABBCache[idx].LastOccluderVertices != nullptr) {
        if (AABBCache[idx].LastOccluderVertices[0] == vertices[0] &&
            AABBCache[idx].LastOccluderVertices[1] == vertices[1] &&
            AABBCache[idx].LastOccluderVertices[2] == vertices[2] &&
            AABBCache[idx].LastOccluderVertices[3] == vertices[3] &&
            AABBCache[idx].LastOccluderVertices[4] == vertices[4] &&
            AABBCache[idx].LastOccluderVertices[5] == vertices[5] &&
            AABBCache[idx].LastOccluderVertices[6] == vertices[6] &&
            AABBCache[idx].LastOccluderVertices[7] == vertices[7] &&
            AABBCache[idx].LastOccluderVertices[8] == vertices[8]) {
          return AABBCache[idx].OccluderMinExtent;
        }
      }
    }
  }

  __m128 refMin = _mm_set1_ps(std::numeric_limits<float>::infinity());
  __m128 refMax = _mm_set1_ps(-std::numeric_limits<float>::infinity());

  const float* pVertices = vertices;
  unsigned int nVert3 = nVert * 3;
  for (unsigned int idx = 0; idx < nVert3; idx += 3) {
    __m128 p = _mm_setr_ps(pVertices[0], pVertices[1], pVertices[2], 1.0f);
    refMin = _mm_min_ps(p, refMin);
    refMax = _mm_max_ps(p, refMax);

    pVertices += 3;
  }

  OccluderAABB& cache = AABBCache[AABBNextStoreIdx];
  cache.OccluderMinExtent[0] = refMin;
  cache.OccluderMinExtent[1] = _mm_sub_ps(refMax, refMin);
  cache.LastOccluderVertices = vertices;
  AABBNextStoreIdx++;
  AABBNextStoreIdx &= (Config_AABBCacheSize - 1);  // store 4 cache only
  mValidAABB++;
  mValidAABB = std::min<int>(mValidAABB, Config_AABBCacheSize);

  return cache.OccluderMinExtent;
}

void RapidRasterizer::RecordFlushAction() {
  if (mOccluderCenter->mCurrentOccNum >= 1) {
    mOccluderCenter->mFlushOccluders.push_back(mOccluderCenter->mCurrentOccNum);
  }
}

void RapidRasterizer::EnablePriorityQueue(bool value) {
  this->mSortByPriorityQueue = value;
}

void RapidRasterizer::SyncOccluderPVS(bool* value) {
  int frameOccluderNum = mOccluderCenter->mCurrentOccNum;
  if (mInRenderingState == false)
    frameOccluderNum = mOccluderCenter->mLastFinalRendered;

  for (int idx = 0; idx < frameOccluderNum; idx++) {
    value[idx] = mOccluderCenter->mPool[idx]->potentialVisible;
  }
  value[frameOccluderNum] =
      mOccluderCenter
          ->LatestFrameInterleaveDraw;  // m_instance->mInterleave.CurrentFrameInterleaveDrawing;
}

void RapidRasterizer::BeforeQueryTreatTrueAsCulled() {
  m_instance->mOccludeeTrueAsCulled = true;
}

void RapidRasterizer::ConfigQueryChildData(uint16_t* value) {
  this->m_instance->mOccludeeTreeData = value;
}

void OccluderManager::OnRenderEnd() {
  mLastFinalRendered = mCurrentOccNum;

  mCurrentOccNum = 0;
  mCurrentRasterizedNum = 0;
}

}  // namespace CullingEngine
