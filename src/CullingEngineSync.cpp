#include <cstdint>
#include <string>

#include "CullingEngineAPI.h"
#include "CullingEngineCore.h"
#include "CullingEngineLogger.h"
#include "OccluderManager.h"
#include "OccluderQuadDecomposition.h"

using CullingEngine::CullingEnginePrivate;

#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)

static int replayConfig = 0;
static int replayFrameNum = 0;
static uint64_t replaySetting = 0;
static float* replayResult = nullptr;

static std::string gImageSavePath;

static bool SocReplay(const char* file_path) {
  if (file_path == nullptr) {
    return false;
  }
  if (replayResult == nullptr) {
    return false;
  }

  CullingEnginePrivate* instance =
      (CullingEnginePrivate*)CullingEngineInit(384, 96, 1.0f);
  if (instance == nullptr) {
    return false;
  }

  bool replied = instance->Replay(file_path, replayConfig, replayFrameNum,
                                  replaySetting, replayResult);
  instance->SocDestroy();
  return replied;
}
#endif

static bool CullingEngineSyncImpl(void* pCullingEngine, unsigned int id,
                                  void* param) {
#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)
  if (id == 9999) {
    if (param == nullptr) {
      return false;
    }
    replayResult = (float*)param;
    return true;
  }
  if (id == 10000) {
    if (param == nullptr) {
      return false;
    }
    replayConfig = *((int*)param);
    return true;
  } else if (id == 10001) {
    if (param == nullptr) {
      return false;
    }
    replayFrameNum = *((int*)param);
    return true;
  } else if (id == 10003) {
    if (param == nullptr) {
      return false;
    }
    replaySetting = *((uint64_t*)param);
    return true;
  } else if (id == 10002) {
    if (param == nullptr) {
      return false;
    }
    bool replied = SocReplay((const char*)param);
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance != nullptr) {
      instance->SocDestroy();
    }
    return replied;
  }
#endif

  if (id == CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET) {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }

    bool* value = reinterpret_cast<bool*>(param);
    if (value == nullptr) {
      return false;
    }
    instance->m_rapidRasterizer->SyncOccluderPVS(value);
    return true;
  }

  if (id == CULLING_ENGINE_SET_QUERY_TREE_DATA) {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }

    uint16_t* value = reinterpret_cast<uint16_t*>(param);
    if (value == nullptr) {
      return false;
    }
    instance->m_rapidRasterizer->ConfigQueryChildData(value);
    return true;
  }

  if (id == CULLING_ENGINE_GET_IS_SAME_CAMERA) {
    CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
    if (instance == nullptr) {
      return false;
    }

    bool* value = reinterpret_cast<bool*>(param);
    if (value == nullptr) {
      return false;
    }
    *value = instance->m_frameInfo->IsSameCameraWithPrev;
    return true;
  }
  if (id == CULLING_ENGINE_GET_LOG) {
    char* msg = reinterpret_cast<char*>(param);
    if (msg == nullptr) {
      return false;
    }
    return CullingEngineLogger::Singleton.GetLog(msg);
  } else if (id == CULLING_ENGINE_PRINT_LOG) {
    CullingEngineLogger::Singleton.PrintLog();
    return true;
  } else if (id == CULLING_ENGINE_SET_PRINT_LOG_IN_GAME) {
    int* value = reinterpret_cast<int*>(param);
    if (value != nullptr) {
      CullingEngineLogger::Singleton.SetStoreMsgConfig((*value) == 1);
      return true;
    }
    return false;
  } else if (id == CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH) {
    char* value = reinterpret_cast<char*>(param);
    if (value != nullptr) {
      std::string output = std::string(value);
      CullingEngineFrameCapture::Singleton.SetCaptureOutputPath(output);
#if defined(CULLING_ENGINE_NATIVE)
      CullingEngine::OccluderQuad::SetOutputPath(output);
#endif
      return true;
    }
    return false;
  }
#if defined(CULLING_ENGINE_NATIVE)
  else if (id == 888) {
    char* value = reinterpret_cast<char*>(param);
    if (value != nullptr) {
      std::string output = std::string(value);
      CullingEngine::OccluderQuad::SetOutputPath(output);
      CullingEngine::OccluderQuad::SetSaveModel(1);
      return true;
    }
    return false;
  } else if (id == CULLING_ENGINE_BAKE_MESH_SIMPLIFY_CONFIG) {
    int* value = reinterpret_cast<int*>(param);
    if (value != nullptr) {
      CullingEngine::OccluderQuad::AllowPlanarQuadMerge(value[0] != 0);
      if (value[1] >= 0 && value[1] <= 2) {
        CullingEngine::OccluderQuad::SetTerrainGridOptimization(value[1]);
      }
      if (value[2] >= 80 && value[2] <= 89) {
        CullingEngine::OccluderQuad::SetTerrainRectangleAngle(value[2]);
      }
      if (value[3] >= 1 && value[3] <= 10) {
        CullingEngine::OccluderQuad::SetTerrainRectangleMergeAngle(value[3]);
      }
      return true;
    }
    return false;
  }
#endif
  else if (id == CULLING_ENGINE_GET_VERSION) {
    int* value = reinterpret_cast<int*>(param);
    if (value != nullptr) {
      union w {
        int a;
        char b;
      } c;
      c.a = 1;
      if (c.b == 1) {
        *value = CullingEngine::VERSION_MAJOR * 10 + CullingEngine::VERSION_SUB;
      } else {
        *value = 0;  // special version to indicate it is a big endian system.
        return false;
      }
    }

    return true;
  }

  CullingEnginePrivate* instance = (CullingEnginePrivate*)pCullingEngine;
  if (instance == nullptr) {
    return false;
  }

  if (param == nullptr) {
    return false;
  }
  if (id == CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT) {
    if (param == nullptr) {
      return false;
    }
    int* input = (int*)param;
    input[0] = instance->m_frameInfo->Width;
    input[1] = instance->m_frameInfo->Height;
    return true;
  } else if (id == CULLING_ENGINE_GET_DEPTH_MAP) {
    unsigned char* data = (unsigned char*)param;
    return instance->DoDumpDepthMap(data,
                                    CullingEngine::DumpImageMode::kDumpFull);
  } else if (
      id == CULLING_ENGINE_SET_COHERENT_MODE_SMALL_ROTATE_DOT_ANGLE_THRESHOLD) {
    instance->SmallRotateDotAngleThreshold = *(float*)param;
  } else if (
      id == CULLING_ENGINE_SET_COHERENT_MODE_LARGE_ROTATE_DOT_ANGLE_THRESHOLD) {
    instance->LargeRotateDotAngleThreshold = *(float*)param;
  } else if (id ==
             CULLING_ENGINE_SET_COHERENT_MODE_CAMERA_DISTANCE_NEAR_THRESHOLD) {
    instance->CameraNearDistanceThreshold = *(float*)param;
  } else if (id == CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT) {
    unsigned int* widthHeight = reinterpret_cast<unsigned int*>(param);
    if (widthHeight == nullptr) {
      return false;
    }
    return instance->Resize(widthHeight[0], widthHeight[1]);
  } else if (id == CULLING_ENGINE_GET_MEMORY_USED) {
    int* output = (int*)param;
    *output = (int)(instance->GetMemorySizeInBytes() >> 10);

    return true;
  } else if (id == CULLING_ENGINE_SAVE_DEPTH_MAP) {
    unsigned char* fullImgData = reinterpret_cast<unsigned char*>(param);
    if (fullImgData != nullptr) {
      const char* imageSavePath =
          gImageSavePath.empty() ? nullptr : gImageSavePath.c_str();
      bool r = instance->SaveColorImage(fullImgData, imageSavePath);
      gImageSavePath.clear();
      return r;
    }
    return false;
  } else if (id == CULLING_ENGINE_SAVE_DEPTH_MAP_PATH) {
    const char* value = reinterpret_cast<const char*>(param);
    gImageSavePath = value != nullptr ? value : "";
    return true;
  } else {
    return false;
  }

  return true;
}

bool CullingEngineSync(void* pCullingEngine, unsigned int id, void* param) {
  try {
    return CullingEngineSyncImpl(pCullingEngine, id, param);
  } catch (...) {
    try {
      CULLING_ENGINE_LOG_ERROR(
          "CullingEngineSync failed due to an unhandled C++ exception");
    } catch (...) {
    }
    return false;
  }
}
