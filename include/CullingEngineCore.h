#pragma once

#include <memory>

#include "CullingEngineAPI.h"
#include "CullingEngineFrameInfo.h"
#include "OccluderManager.h"
#include "OccluderQuadDecomposition.h"
#include "PlatformSIMD.h"

namespace CullingEngine {

class CullingEnginePrivate {
 public:
  CullingEnginePrivate();
  ~CullingEnginePrivate();

 public:
  float SmallRotateDotAngleThreshold =
      0.9999f;  // rotating if angle dot value < 0.9999f, which means > 0.8
                // degree
  float LargeRotateDotAngleThreshold = 0.9962f;  // rotating 5 degree

  float CameraNearDistanceThreshold = -1;
  CullingEngine::AlgoEnum algoApproachMask =
      CullingEngine::AlgoEnum::kRasterizerFullTriangle;

  bool mResolutionChanged = false;
  uint16_t mRapidCoherentMode = 0;

  // CullingEngine frame info
  CullingEngine::CullingEngineFrameInfo* m_frameInfo = nullptr;

  CullingEngine::RapidRasterizer* m_rapidRasterizer;

  PFN_CullingEngineMalloc m_MallocFunc;
  PFN_CullingEngineFree m_FreeFunc;
  bool m_UsesCustomAllocator = false;

 public:
  CullingEnginePrivate(const CullingEnginePrivate&) = delete;
  CullingEnginePrivate(CullingEnginePrivate&&) = delete;
  CullingEnginePrivate& operator=(const CullingEnginePrivate&) = delete;
  CullingEnginePrivate& operator=(CullingEnginePrivate&&) = delete;

 public:
  // framebuffer resolution, near clip distance
  bool Resize(unsigned int width, unsigned int height);
  void SetNearPlane(float nearPlane);

  // start new frame
  void StartNewFrame(const float* CameraPos, const float* ViewDir,
                     const float* ViewProj, bool bRowMajorMat = false);

  // batch query
  bool BatchQuery(const float* bbox, unsigned int nPrim, bool* results);

  void ConfigPerformanceMode(unsigned int configValue);
  bool SetConfig(unsigned int configTarget, unsigned int configValue);

  // set approach
  void SetAlgoApproach(CullingEngine::AlgoEnum config);

  bool OnFrameCaptureSet(int configValue);

  // Dump and Replay
 public:
  bool Replay(const char* file_path, int config, int frameNum,
              uint64_t replaySetting, float* replayResult);
  bool SaveColorImage(unsigned char* fullImgData, const char* path);
  bool DoDumpDepthMap(unsigned char* data, CullingEngine::DumpImageMode mode);

 public:
  void SocDestroy();
  bool SocConfig(unsigned int width, unsigned int height, float nearPlane);

 public:
  size_t GetMemorySizeInBytes();
  void SetRenderType(int renderType);
};

}  // namespace CullingEngine
