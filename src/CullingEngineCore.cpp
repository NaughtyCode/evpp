#include "CullingEngineCore.h"

#include <vector>

#include "CullingEngineAPI.h"
#if defined(CULLING_ENGINE_NATIVE)
#include <fstream>
#include <sstream>
#endif
#include "CullingEngineLogger.h"
#include "MathUtility.h"

#if defined(CULLING_ENGINE_PLATFORM_ANDROID)
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <fstream>
#endif

#include "CullingEngineMeshBaker.h"
#include "OccluderQuadDecomposition.h"

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
#pragma warning(disable : 4996)
#endif

namespace CullingEngine {
// CullingEnginePrivate
CullingEnginePrivate::CullingEnginePrivate() {
  m_MallocFunc = nullptr;
  m_FreeFunc = nullptr;

  // frame info
  m_frameInfo = new CullingEngine::CullingEngineFrameInfo();
  // create rapid rasterizer
  m_rapidRasterizer = new CullingEngine::RapidRasterizer();

  this->m_frameInfo->m_rapidRasterizer = this->m_rapidRasterizer;

  memset(m_frameInfo->CameraPos, 0, 3 * sizeof(float));

  SetAlgoApproach(CullingEngine::AlgoEnum::kRasterizerFullTriangle);

  this->ConfigPerformanceMode(CULLING_ENGINE_RENDER_MODE_COHERENT_FAST);
}

CullingEnginePrivate::~CullingEnginePrivate() {
  delete m_frameInfo;
  delete m_rapidRasterizer;
}

// PScene: 1152x96 -> 306 us
//		  1152x192-> 342 us
//		  576x192 -> 300 us
bool CullingEnginePrivate::Resize(unsigned int width, unsigned int height) {
  if (width < 64 || height < 8) {
    return false;
  }
  if (width > 65535 || height > 65535) {
    return false;
  }
  if ((width & 63) != 0 || (height & 7) != 0) {
    return false;
  }

  // check whether width & height are the same as before.
  if (width == m_frameInfo->Width && height == m_frameInfo->Height) {
    return false;
  }

  m_rapidRasterizer->SetResolution(width, height);

  // keep tracking in CullingEngineFrameInfo
  m_frameInfo->Width = width;
  m_frameInfo->Height = height;

  this->mResolutionChanged = true;
  return true;
}

void CullingEnginePrivate::SetNearPlane(float nearPlane) {
  if (nearPlane < CULLING_ENGINE_MIN_NEAR_PLANE) {
    nearPlane = CULLING_ENGINE_MIN_NEAR_PLANE;
  }

  // keep tracking in CullingEngineFrameInfo
  m_frameInfo->NearPlane = nearPlane;
  m_rapidRasterizer->SetNearPlane(nearPlane);
}

void CullingEnginePrivate::StartNewFrame(const float* CameraPos,
                                         const float* ViewDir,
                                         const float* ViewProj,
                                         bool bRowMajorMat) {
  if (CameraPos == nullptr || ViewDir == nullptr || ViewProj == nullptr) {
    return;
  }

  CullingEngine::Vector3f ViewDirV =
      CullingEngine::Vector3f(ViewDir[0], ViewDir[1], ViewDir[2]);
  CullingEngine::Vector3f lastDir = m_frameInfo->mCameraViewDir;
  m_frameInfo->mCameraViewDir = ViewDirV.Normalize();
  float viewDot = lastDir.Dot(m_frameInfo->mCameraViewDir);
  bool IsRotating = viewDot < this->SmallRotateDotAngleThreshold;
  bool IsRotatingLarge = viewDot < this->LargeRotateDotAngleThreshold;

  // cache the camera position and view-projection matrix
  float cameraPosSquareDis =
      CullingEngine::FloatArray::CalculateSquareDistance3(
          m_frameInfo->CameraPos, CameraPos);

  bool IsCameraDistanceNear = true;
  if (this->CameraNearDistanceThreshold > 0) {
    IsCameraDistanceNear =
        cameraPosSquareDis <=
        this->CameraNearDistanceThreshold * this->CameraNearDistanceThreshold;
  }

  m_frameInfo->StartNewFrame();
  // set ViewProj
  bool sameVP = false;
  if (cameraPosSquareDis == 0) {
    sameVP = CullingEngine::FloatArray::ContainSameData16(
        m_frameInfo->ViewProjArray, ViewProj);
  }
  if (sameVP == false) {
    memcpy(m_frameInfo->CameraPos, CameraPos, 3 * sizeof(float));
    memcpy(m_frameInfo->ViewProjArray, ViewProj, 16 * sizeof(float));

    m_frameInfo->IsSameCameraWithPrev = false;

    // update ViewProjT if VP is changed!
    m_frameInfo->m_rapidRasterizer->mViewProjT.UpdateTranspose(ViewProj,
                                                               bRowMajorMat);
  } else {
    m_frameInfo->IsSameCameraWithPrev = !this->mResolutionChanged;
  }

  if (m_frameInfo->mIsRecording) {
    m_frameInfo->mIsRecording = false;
    m_frameInfo->StopFrameCapture();
  } else {
    if (m_frameInfo->mCaptureFrame) {
      m_frameInfo->mCaptureFrame = false;
      m_frameInfo->StartRecordFrame(CameraPos, ViewDir, ViewProj);
    }
  }

  bool criticalFrame = false;
  bool isRotating = false;
  {
    if (this->mRapidCoherentMode > 0) {
      if (IsRotatingLarge || IsCameraDistanceNear == false) {
        criticalFrame = true;
      } else {
        isRotating = IsRotating;
      }
    }

    criticalFrame |= ViewDir[0] == 0 && ViewDir[1] == 0 && ViewDir[2] == 0;
  }

  criticalFrame |= this->mResolutionChanged;
  this->mResolutionChanged = false;

  this->m_rapidRasterizer->OnNewFrame(m_frameInfo->FrameCounter, criticalFrame,
                                      isRotating);
}

bool CullingEnginePrivate::BatchQuery(const float* bbox, unsigned int nMesh,
                                      bool* results) {
  if (bbox == nullptr || results == nullptr || nMesh == 0) {
    return false;
  }

  {
    // use rapid rasterizer only if plain rasterizer didn't render anything.
    m_rapidRasterizer->BatchQuery(bbox, nMesh, results);
  }

  if (m_frameInfo->mIsRecording) {
    m_frameInfo->RecordOccludee(bbox, nMesh);
  }

  return true;
}

size_t CullingEnginePrivate::GetMemorySizeInBytes() {
  size_t memorySizeInBytes = 0;

  // self
  memorySizeInBytes += sizeof(CullingEnginePrivate);

  // RapidRasterizer
  memorySizeInBytes += m_rapidRasterizer->GetMemorySizeInBytes();

  if (m_frameInfo) {
    memorySizeInBytes += m_frameInfo->GetMemorySizeInBytes();
  }

  return memorySizeInBytes;
}

void CullingEnginePrivate::SocDestroy() {
  PFN_CullingEngineFree freeFunc = m_FreeFunc;
  const bool useCustomAllocator = m_UsesCustomAllocator;
  this->~CullingEnginePrivate();
  if (useCustomAllocator && freeFunc != nullptr) {
    freeFunc(this);
  } else {
    ::operator delete(this);
  }
}

bool CullingEnginePrivate::SocConfig(unsigned int width, unsigned int height,
                                     float nearPlane) {
  if (nearPlane < CULLING_ENGINE_MIN_NEAR_PLANE) {
    nearPlane = CULLING_ENGINE_MIN_NEAR_PLANE;
  }

  if (!this->Resize(width, height)) {
    return false;
  }
  this->SetNearPlane(nearPlane);

  return true;
}

}  // namespace CullingEngine
