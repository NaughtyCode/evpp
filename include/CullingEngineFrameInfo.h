#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "MathUtility.h"

class CullingEngineFrameCapture {
 public:
  static CullingEngineFrameCapture Singleton;

 public:
  std::string OutputDir = "";
  std::string mLatestCaptureDepthMapFilename = "";

  std::string mLatestCapFilename = "";
  std::string mLatestCapFilenameReady = "";

 public:
  void SetLatestCaptureDepthMapFilename(
      const std::string& latestCaptureDepthMapFilename);
  void SetLatestCapFilename(const std::string& latestCapFilename);
  void SetLatestCapFilenameReady(const std::string& latestCapFilename);
  void UpdateLatestCapFilename();
  std::string GetLatestCaptureDepthMapFilename();
  std::string GetLatestCapFilename();
  void ResetLatestCaptureDepthMapFilename();
  void ResetLatestCapFilename();
  bool HasLatestCaptureDepthMapFilename();
  bool HasLatestCapFilename();
  std::string GetCaptureOutputPath();

  static bool IsPathValid(const std::string& pathname);
  static bool CreateDirectorySingle(const std::string& pathname);
  static bool CreateDirectories(const std::string& pathname);
  static std::string GetOutputDirectory();

  void SetCaptureOutputPath(std::string output);

 private:
  std::mutex mMutex;
};

namespace CullingEngine {
class RapidRasterizer;
}  // namespace CullingEngine

namespace CullingEngine {
enum AlgoEnum {
  kRasterizerFullTriangle = 1 << 2,

#if defined(CULLING_ENGINE_NATIVE_DEBUG)
  kRasterizerFullTriangle2 = 1 << 3,
  kRasterizerFullTriangle3 = 1 << 4,
  kRasterizerFullTriangle4 = 1 << 5,
  kRasterizerFullTriangle5 = 1 << 6,
#endif
};

enum DumpImageMode {
  kDumpFull = 0,
#ifdef CULLING_ENGINE_NATIVE_DEBUG
  kDumpHiz = 9,
  kDumpBlockMask = 10,
  kCheckerboardBlackPattern = 11,
  kCheckerboardWhitePattern = 12,
  kCheckerboardOddColumn = 13,
  kCheckerboardEvenColumn = 14,
  kCheckerboardOddBlack = 15,
  kCheckerboardEvenBlack = 16,
  kCheckerboardEvenWhite = 17,
  kCheckerboardOddWhite = 18,
#endif
};

struct OccluderMesh {
  unsigned int VerticesNum = 0;
  unsigned int QuadSafeBatchNum = 0;
  unsigned int TriangleBatchIdxNum = 0;
  uint16_t SuperCompress = 0;

  uint16_t EnableBackface = 0;
  const float* Vertices = nullptr;
  const uint16_t* Indices = nullptr;
};

class CullingEngineFrameInfo {
 public:
  CullingEngineFrameInfo();
  ~CullingEngineFrameInfo();

 public:
  void StartNewFrame();

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
  void SubmitOccluder(const float* vertices, const unsigned short* indices,
                      unsigned int nVert, unsigned int nIdx,
                      const float* localToWorld, bool bRowMajorLocalToWorld,
                      bool bEnableBackfaceCull);
#endif  // End of CULLING_ENGINE_SUPPORT_ALL_FEATURES

  void RecordOccludee(const float* vertices, unsigned int num);

 public:
  uint32_t Width = 0;
  uint32_t Height = 0;
  float NearPlane = 0.0f;

  uint8_t IsSameCameraWithPrev : 1;
  uint8_t mCaptureFrame : 1;

  uint64_t FrameCounter = START_FRAME_COUNT;

  CullingEngine::Vector3f mCameraViewDir;
  float CameraPos[3] = {};

  float ViewProjArray[16] = {};

  CullingEngine::RapidRasterizer* m_rapidRasterizer = nullptr;

  std::string mOutputSaveCap = "";
  uint8_t mIsRecording : 1;

  FILE* mFileWriter = nullptr;

 public:
  void StopFrameCapture();
  void StartRecordFrame(const float* CameraPos, const float* ViewDir,
                        const float* ViewProj);
  void RecordOccluder(const float* vertices, const unsigned short* indices,
                      unsigned int nVert, unsigned int nIdx,
                      const float* localToWorld, int backfaceCull);

 public:
  size_t GetMemorySizeInBytes() {
    size_t memorySizeInBytes = 0;
    memorySizeInBytes += sizeof(CullingEngineFrameInfo);
    memorySizeInBytes += mOutputSaveCap.size() * sizeof(char);
    return memorySizeInBytes;
  }
};
}  // namespace CullingEngine
