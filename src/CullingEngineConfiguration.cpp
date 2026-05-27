#include "CullingEngineCore.h"
#include "CullingEngineLogger.h"

namespace CullingEngine {
void CullingEnginePrivate::ConfigPerformanceMode(unsigned int configValue) {
  if (configValue <= CULLING_ENGINE_RENDER_MODE_COHERENT_FAST) {
    this->mRapidCoherentMode = configValue;
    int octuple = 0;
    // if (configValue == CULLING_ENGINE_RENDER_MODE_FULL) //do nothing
    if (configValue == CULLING_ENGINE_RENDER_MODE_COHERENT_FAST) {
      octuple = 4;
    } else if (configValue == CULLING_ENGINE_RENDER_MODE_COHERENT) {
      octuple = 5;
    }

    this->m_rapidRasterizer->ConfigCoherentMode(octuple);  // no accurate
  }
}

bool CullingEnginePrivate::SetConfig(unsigned int configTarget,
                                     unsigned int configValue) {
  bool handled = true;

  if (configTarget == CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER) {
    m_rapidRasterizer->FlushCachedOccluder(
        m_rapidRasterizer->mOccluderCenter->mCurrentOccNum);
    m_rapidRasterizer->RecordFlushAction();
    return true;
  } else if (configTarget == CULLING_ENGINE_RENDER_MODE) {
    if (configValue <= CULLING_ENGINE_RENDER_MODE_COHERENT_FAST) {
      this->ConfigPerformanceMode(configValue);
      CULLING_ENGINE_LOG_INFO("Performance mode set: coherentMode=%d",
                          (int)this->mRapidCoherentMode);
    } else if (configValue == CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE) {
      this->m_rapidRasterizer->SetRenderType(-1);
    } else {
      return false;
    }
  } else if (configTarget == CULLING_ENGINE_CAPTURE_FRAME) {
    return this->OnFrameCaptureSet(configValue);
  } else if (configTarget == CULLING_ENGINE_SHOW_CULLED) {
    if (configValue != 0 && configValue != 1) return false;
    this->m_rapidRasterizer->mShowCulled = configValue == 1;
  } else if (configTarget == CULLING_ENGINE_SET_CCW) {
    if (configValue != 0 && configValue != 1) return false;
    bool IsModelCCW = configValue == 1;
    this->m_rapidRasterizer->SetCCW(IsModelCCW);
  } else if (configTarget == CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP) {
    if (configValue != 0 && configValue != 1) return false;
    m_rapidRasterizer->ShowOccludeeInDepthmap(configValue);
  } else if (configTarget == CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST) {
    if (configValue != 0 && configValue != 1) return false;
    m_rapidRasterizer->mBackFaceCullOffFirst = configValue == 1;
    CULLING_ENGINE_LOG_INFO(
        "Back-face cull off first set to %d",
        (int)this->m_rapidRasterizer->mBackFaceCullOffFirst);
  } else if (configTarget == CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE) {
    if (configValue != 0 && configValue != 1) return false;
    this->m_rapidRasterizer->EnablePriorityQueue(configValue == 1);
    CULLING_ENGINE_LOG_INFO("Occluder priority queue enabled=%d",
                        configValue == 1 ? 1 : 0);
    return true;
  } else if (configTarget == CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER) {
    if (configValue != 0 && configValue != 1) return false;
    this->m_rapidRasterizer->PrintNumberOfOccluderOnce = configValue == 1;
    CULLING_ENGINE_LOG_INFO(
        "Print active occluder debug set to %d",
        configValue);
    return true;
  } else {
    handled = false;
  }

  if (handled == false) {
    CULLING_ENGINE_LOG_WARNING(
        "Unsupported config: target=%d value=%d",
        configTarget, configValue);
    return false;
  }

  return true;
}

void CullingEnginePrivate::SetAlgoApproach(CullingEngine::AlgoEnum config) {
  this->algoApproachMask = config;
}

bool CullingEnginePrivate::OnFrameCaptureSet(int configValue) {
  if (configValue == 0) return false;
  std::string outputDir = CullingEngineFrameCapture::GetOutputDirectory();
  if (outputDir == "") {
    CULLING_ENGINE_LOG_WARNING(
        "Invalid frame capture output path: %s",
        CullingEngineFrameCapture::Singleton.OutputDir.c_str());
    return false;
  }

  m_frameInfo->mCaptureFrame = true;
  return true;
}

void CullingEnginePrivate::SetRenderType(int renderType) {
  this->m_rapidRasterizer->SetRenderType(renderType);
}

}  // namespace CullingEngine
