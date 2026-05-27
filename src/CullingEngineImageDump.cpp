#include "CullingEngineImageDump.h"

#include <ctime>
#include <string>

#include "CullingEngineCore.h"
#include "CullingEngineLogger.h"

#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)

bool CullingEngine::CullingEnginePrivate::SaveColorImage(
    unsigned char* fullImgData, const char* path) {
  if (path != nullptr) {
    std::string fileName = path;
    bool bSaved = DumpOccluderOccludeeColorImage(
        fileName, fullImgData, m_frameInfo->Width, m_frameInfo->Height);
    if (bSaved) {
      CullingEngineFrameCapture::Singleton.SetLatestCaptureDepthMapFilename(
          fileName);
      CULLING_ENGINE_LOG_INFO("Captured depth PNG: %s", fileName.c_str());
    }
    return bSaved;
  }

  std::string outputDir = CullingEngineFrameCapture::GetOutputDirectory();
  if (outputDir.empty()) {
    return false;
  }
  time_t now = time(0);
  tm fallbackTime = {};
  tm* ltm = localtime(&now);
  if (ltm == nullptr) {
    ltm = &fallbackTime;
  }
  int year = 1900 + ltm->tm_year;
  int month = 1 + ltm->tm_mon;
  int day = ltm->tm_mday;
  int hour = ltm->tm_hour;
  int minute = ltm->tm_min;
  int sec = ltm->tm_sec;
  std::string fileName = outputDir + "//CullingEngine" + std::to_string(year) +
                         "_" + std::to_string(month) + "_" +
                         std::to_string(day) + "_" + std::to_string(hour) +
                         "_" + std::to_string(minute) + "_" +
                         std::to_string(sec) + ".png";

  bool bSaved = DumpOccluderOccludeeColorImage(
      fileName, fullImgData, m_frameInfo->Width, m_frameInfo->Height);
  if (bSaved) {
    CullingEngineFrameCapture::Singleton.SetLatestCaptureDepthMapFilename(
        fileName);
    CULLING_ENGINE_LOG_INFO("Captured depth PNG: %s", fileName.c_str());
  }
  return bSaved;
}

bool CullingEngine::CullingEnginePrivate::DoDumpDepthMap(
    unsigned char* data, CullingEngine::DumpImageMode mode) {
  if (!data) {
    return false;
  }

  return m_rapidRasterizer->DumpDepthMap(data, mode);
}

bool CullingEngineDumpOccluderOccludeeColorImage(const char* filename,
                                                 unsigned char* inputBuffer,
                                                 unsigned int width,
                                                 unsigned int height) {
  try {
    if (filename == nullptr || inputBuffer == nullptr) {
      return false;
    }

    std::string filenameLocal(filename);
    return DumpOccluderOccludeeColorImage(filenameLocal, inputBuffer, width,
                                          height);
  } catch (...) {
    return false;
  }
}

#else  // Else of CULLING_ENGINE_NATIVE_DEBUG && CULLING_ENGINE_NATIVE
bool CullingEngine::CullingEnginePrivate::SaveColorImage(
    unsigned char* fullImgData, const char* path) {
  (void)fullImgData;
  (void)path;
  return false;
}

bool CullingEngine::CullingEnginePrivate::DoDumpDepthMap(
    unsigned char* data, CullingEngine::DumpImageMode mode) {
  if (!data) {
    return false;
  }

  return m_rapidRasterizer->DumpDepthMap(data, mode);
}

bool CullingEngineDumpOccluderOccludeeColorImage(const char* filename,
                                                 unsigned char* inputBuffer,
                                                 unsigned int width,
                                                 unsigned int height) {
  (void)filename;
  (void)inputBuffer;
  (void)width;
  (void)height;
  return false;
}

#endif  // End of CULLING_ENGINE_NATIVE_DEBUG && CULLING_ENGINE_NATIVE
