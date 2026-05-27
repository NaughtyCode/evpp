#include "CullingEngineAPI.h"

#include <new>

#include "CullingEngineCore.h"
#include "CullingEngineLogger.h"
#include "MathUtility.h"
#include "OccluderManager.h"
#include "OccluderQuadDecomposition.h"
#include "SoftwareRasterizer.h"

using CullingEngine::CullingEnginePrivate;

namespace {
void LogUnhandledApiException(const char* apiName) noexcept {
  try {
    CULLING_ENGINE_LOG_ERROR("%s failed due to an unhandled C++ exception",
                             apiName);
  } catch (...) {
  }
}
}  // namespace

void* CullingEngineInit(unsigned int width, unsigned int height,
                        float nearPlane, PFN_CullingEngineMalloc mallocFunc,
                        PFN_CullingEngineFree freeFunc) {
  if ((mallocFunc == nullptr) != (freeFunc == nullptr)) {
    return nullptr;
  }

  void* customMemory = nullptr;
  CullingEnginePrivate* pCullingEngine = nullptr;
  try {
    if (mallocFunc != nullptr) {
      customMemory = mallocFunc(sizeof(CullingEnginePrivate));
      if (customMemory == nullptr) {
        return nullptr;
      }
      pCullingEngine = new (customMemory) CullingEnginePrivate();
      customMemory = nullptr;
      pCullingEngine->m_UsesCustomAllocator = true;
    } else {
      pCullingEngine = new CullingEnginePrivate();
    }

    pCullingEngine->m_MallocFunc = mallocFunc;
    pCullingEngine->m_FreeFunc = freeFunc;

    if (!pCullingEngine->SocConfig(width, height, nearPlane)) {
      pCullingEngine->SocDestroy();
      return nullptr;
    }

#if defined(CULLING_ENGINE_STATIC)
    CULLING_ENGINE_LOG_INFO("CullingEngine started: linkage=static version=%d.%d",
                        CullingEngine::VERSION_MAJOR,
                        CullingEngine::VERSION_SUB);
#else
    CULLING_ENGINE_LOG_INFO("CullingEngine started: linkage=dynamic version=%d.%d",
                        CullingEngine::VERSION_MAJOR,
                        CullingEngine::VERSION_SUB);
#endif

    return pCullingEngine;
  } catch (...) {
    if (pCullingEngine != nullptr) {
      pCullingEngine->SocDestroy();
    } else if (customMemory != nullptr && freeFunc != nullptr) {
      freeFunc(customMemory);
    }
    LogUnhandledApiException("CullingEngineInit");
    return nullptr;
  }
}

bool CullingEngineStartNewFrame(void* pCullingEngine, const float* ViewPos,
                                const float* ViewDir, const float* ViewProj,
                                bool bRowMajorMat) {
  try {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }
    if (ViewPos == nullptr || ViewDir == nullptr || ViewProj == nullptr) {
      return false;
    }

    instance->StartNewFrame(ViewPos, ViewDir, ViewProj, bRowMajorMat);
    return true;
  } catch (...) {
    LogUnhandledApiException("CullingEngineStartNewFrame");
    return false;
  }
}

void CullingEngineRenderBakedOccluder(void* pCullingEngine,
                                      unsigned short* compressedModel,
                                      const float* localToWorld,
                                      bool bRowMajorMat,
                                      int* outRasterizeTrianglesNum) {
  try {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return;
    }
    if (compressedModel == nullptr || localToWorld == nullptr) {
      return;
    }

    // direct call rapid rasterizer
    instance->m_rapidRasterizer->SubmitBakedOccluder(
        compressedModel, localToWorld, bRowMajorMat, outRasterizeTrianglesNum);

    if (instance->m_frameInfo->mIsRecording) {
      bool backfaceCull =
          true;  // this info is not used in backed model recording
      instance->m_frameInfo->RecordOccluder((float*)compressedModel, nullptr, 0,
                                            0, localToWorld, backfaceCull);
    }
  } catch (...) {
    LogUnhandledApiException("CullingEngineRenderBakedOccluder");
  }
}

void CullingEngineRenderOccluder(void* pCullingEngine, const float* vertices,
                                 const unsigned short* indices,
                                 unsigned int nVert, unsigned int nIdx,
                                 const float* localToWorld, bool bRowMajorMat,
                                 bool bEnableBackfaceCull) {
  try {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return;
    }
    if (vertices == nullptr || indices == nullptr || localToWorld == nullptr ||
        nVert == 0 || nIdx == 0 || (nIdx % 3) != 0) {
      return;
    }

    // direct call rapid rasterizer
    bool validMesh = instance->m_rapidRasterizer->SubmitRawOccluder(
        vertices, indices, nVert, nIdx, localToWorld, bRowMajorMat,
        bEnableBackfaceCull);

    if (instance->m_frameInfo->mIsRecording) {
      if (validMesh) {
        instance->m_frameInfo->RecordOccluder(
            vertices, indices, nVert, nIdx, localToWorld, bEnableBackfaceCull);
      }
    }
  } catch (...) {
    LogUnhandledApiException("CullingEngineRenderOccluder");
  }
}

bool CullingEngineQueryOccludees(void* pCullingEngine, const float* bbox,
                                 unsigned int nMesh, bool* results) {
  try {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }
    if (bbox == nullptr || results == nullptr || nMesh == 0) {
      return false;
    }

    // batchQuery would do input check
    return instance->BatchQuery(bbox, nMesh, results);
  } catch (...) {
    LogUnhandledApiException("CullingEngineQueryOccludees");
    return false;
  }
}

static void CheckRecording(CullingEnginePrivate* instance, int occluderNum) {
  if (instance->m_frameInfo->mIsRecording) {
    int occNum = occluderNum;
    if (occNum == -1) {
      occNum = instance->m_rapidRasterizer->mOccluderCenter->mLastFinalRendered;
    }

    for (int idx = 0; idx < occNum; idx++) {
      CullingEngine::OccluderInput* occ =
          instance->m_rapidRasterizer->mOccluderCenter->mPool[idx];
      if (occ->IsRawMesh && occ->IsValidRawMesh == false) {
        continue;
      }

      instance->m_frameInfo->RecordOccluder(occ->inVtx, occ->inIdx, occ->nVert,
                                            occ->nIdx, occ->modelWorld,
                                            occ->backfaceCull);
    }
  }
}

static void UsePreviousOccluder(CullingEnginePrivate* instance,
                                int sameOccluderNum) {
  instance->m_rapidRasterizer->UsePrevFrameOccluders(sameOccluderNum);
}

static bool CanUsePreviousOccluders(CullingEnginePrivate* instance,
                                    unsigned int sameOccluderNum) {
  if (instance == nullptr || instance->m_rapidRasterizer == nullptr ||
      instance->m_rapidRasterizer->mOccluderCenter == nullptr) {
    return false;
  }

  const int previousOccluderNum =
      instance->m_rapidRasterizer->mOccluderCenter->mLastFinalRendered;
  return sameOccluderNum <= static_cast<unsigned int>(previousOccluderNum);
}

bool CullingEngineSet(void* pCullingEngine, unsigned int ID,
                      unsigned int configValue) {
  try {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }

    switch (ID) {
      case CULLING_ENGINE_BEFORE_QUERY_TREAT_TRUE_AS_CULLED:
        instance->m_rapidRasterizer->BeforeQueryTreatTrueAsCulled();
        return true;

      case CULLING_ENGINE_SET_USE_PREV_DEPTH_BUFFER:
        CheckRecording(instance, -1);
        instance->m_rapidRasterizer->UsePrevDepthData();
        return true;

      case CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS:
        if (!CanUsePreviousOccluders(instance, configValue)) {
          return false;
        }
        CheckRecording(instance, static_cast<int>(configValue));
        UsePreviousOccluder(instance, static_cast<int>(configValue));
        return true;

      case CULLING_ENGINE_DESTROY:
        if (configValue != 1) {
          return false;
        }
        instance->SocDestroy();
        return true;

      default:
        return instance->SetConfig(ID, configValue);
    }
  } catch (...) {
    LogUnhandledApiException("CullingEngineSet");
    return false;
  }
}

void CullingEngineSetNearPlane(void* pCullingEngine, float nearPlane) {
  CullingEnginePrivate* pPrivate = (CullingEnginePrivate*)pCullingEngine;
  if (!pPrivate) {
    return;
  }

  pPrivate->SetNearPlane(nearPlane);
}

float CullingEngineGetNearPlane(void* pCullingEngine) {
  CullingEnginePrivate* pPrivate = (CullingEnginePrivate*)pCullingEngine;
  if (!pPrivate) {
    return CULLING_ENGINE_MIN_NEAR_PLANE;
  }

  return pPrivate->m_frameInfo->NearPlane;
}

void CullingEngineSetIsNeedCheckInFrustum(void* pCullingEngine,
                                          bool bIsNeedCheckInFrustum) {
  CullingEnginePrivate* pPrivate = (CullingEnginePrivate*)pCullingEngine;
  if (!pPrivate || !pPrivate->m_rapidRasterizer ||
      !pPrivate->m_rapidRasterizer->m_instance) {
    return;
  }

  pPrivate->m_rapidRasterizer->m_instance->SetIsNeedCheckInFrustum(
      bIsNeedCheckInFrustum);
}

bool CullingEngineGetIsNeedCheckInFrustum(void* pCullingEngine) {
  CullingEnginePrivate* pPrivate = (CullingEnginePrivate*)pCullingEngine;
  if (!pPrivate || !pPrivate->m_rapidRasterizer ||
      !pPrivate->m_rapidRasterizer->m_instance) {
    return false;
  }

  return pPrivate->m_rapidRasterizer->m_instance->GetIsNeedCheckInFrustum();
}

size_t CullingEngineGetMemorySizeInBytes(void* pCullingEngine) {
  CullingEnginePrivate* pPrivate = (CullingEnginePrivate*)pCullingEngine;
  if (!pPrivate) {
    return 0;
  }

  return pPrivate->GetMemorySizeInBytes();
}
