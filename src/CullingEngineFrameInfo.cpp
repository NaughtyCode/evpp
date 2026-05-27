#include "CullingEngineFrameInfo.h"

#include <cstring>
#include <ctime>

#include "CullingEngineLogger.h"
#include "OccluderManager.h"

namespace CullingEngine {
CullingEngineFrameInfo::CullingEngineFrameInfo() {
  memset(ViewProjArray, 0, 16 * sizeof(float));

  IsSameCameraWithPrev = false;
  mCaptureFrame = false;
  mIsRecording = false;
}

CullingEngineFrameInfo::~CullingEngineFrameInfo() { StopFrameCapture(); }

void CullingEngineFrameInfo::StartNewFrame() { FrameCounter++; }

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES) && \
    defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)

void CullingEngineFrameInfo::SubmitOccluder(
    const float* vertices, const unsigned short* indices, unsigned int nVert,
    unsigned int nIdx, const float* localToWorld, bool bRowMajorLocalToWorld,
    bool bEnableBackfaceCull) {
  {
    if (indices == nullptr) {
      this->m_rapidRasterizer->SubmitBakedOccluder(
          (unsigned short*)vertices, localToWorld, bRowMajorLocalToWorld,
          nullptr);
    } else {
      this->m_rapidRasterizer->SubmitRawOccluder(
          vertices, indices, nVert, nIdx, localToWorld, bRowMajorLocalToWorld,
          bEnableBackfaceCull);
    }
  }

  if (this->mCaptureFrame) {
    RecordOccluder(vertices, indices, nVert, nIdx, localToWorld,
                   bEnableBackfaceCull);
  }
}
#endif

void CullingEngineFrameInfo::RecordOccludee(const float* vertices,
                                            unsigned int num) {
  if (mFileWriter == nullptr) {
    return;
  }

  fprintf(mFileWriter, "%s\n", BATCHED_OCE_HEADER.c_str());
  fprintf(mFileWriter, "%d\n", num);
  const float* boxPtr = vertices;
  for (uint32_t i = 0; i < num; ++i, boxPtr += BBOX_STRIDE) {
    fprintf(mFileWriter, "%f %f %f\n%f %f %f\n", boxPtr[0], boxPtr[1],
            boxPtr[2], boxPtr[3], boxPtr[4], boxPtr[5]);
  }
}

void CullingEngineFrameInfo::StopFrameCapture() {
  if (mFileWriter != nullptr) {
    CULLING_ENGINE_LOG_INFO("Stopped frame capture");
    fclose(mFileWriter);
    mFileWriter = nullptr;
    CullingEngineFrameCapture::Singleton.UpdateLatestCapFilename();
  }
}

void CullingEngineFrameInfo::StartRecordFrame(const float* CameraPos,
                                              const float* ViewDir,
                                              const float* vp) {
  std::string outputDir = CullingEngineFrameCapture::GetOutputDirectory();

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
  std::string timeStamp = std::to_string(year) + "_" + std::to_string(month) +
                          "_" + std::to_string(day) + "_" +
                          std::to_string(hour) + "_" + std::to_string(minute) +
                          "_" + std::to_string(sec);

  std::string file_name = outputDir + CAPTURE_PREFIX +
                          std::to_string(this->FrameCounter) + "_" + timeStamp +
                          CAPTURE_APPENDIX;
  if (mOutputSaveCap != "") {
    file_name = outputDir + mOutputSaveCap;
  }

  mFileWriter = fopen(file_name.c_str(), "w");
  if (mFileWriter == nullptr) {
    return;
  }

  {
    CULLING_ENGINE_LOG_INFO("Recording frame capture: %s", file_name.c_str());
    CullingEngineFrameCapture::Singleton.SetLatestCapFilenameReady(file_name);
  }

  this->mIsRecording = true;

  fprintf(mFileWriter, "%s\n", FB_SETTING_HEADER.c_str());
  int cw = m_rapidRasterizer->GetCW() * 1000000;
  fprintf(mFileWriter, "%d %d %f\n", this->Width, this->Height + cw,
          this->NearPlane);

  fprintf(mFileWriter, "Frame\n");
  fprintf(mFileWriter, "1\n");
  fprintf(mFileWriter, "%s\n", CAM_POS_HEADER.c_str());
  fprintf(mFileWriter, "%f %f %f %f %f %f\n", CameraPos[0], CameraPos[1],
          CameraPos[2], ViewDir[0], ViewDir[1], ViewDir[2]);

  fprintf(mFileWriter, "%s\n", VIEW_PROJ_HEADER.c_str());

  fprintf(mFileWriter, "%f %f %f %f\n", vp[0], vp[1], vp[2], vp[3]);
  fprintf(mFileWriter, "%f %f %f %f\n", vp[4], vp[5], vp[6], vp[7]);
  fprintf(mFileWriter, "%f %f %f %f\n", vp[8], vp[9], vp[10], vp[11]);
  fprintf(mFileWriter, "%f %f %f %f\n", vp[12], vp[13], vp[14], vp[15]);
}

void CullingEngineFrameInfo::RecordOccluder(
    const float* vertices, const unsigned short* indices, unsigned int nVert,
    unsigned int nIdx, const float* localToWorld, int backfaceCull) {
  auto fptr = mFileWriter;
  if (fptr == nullptr) {
    return;
  }
  uint32_t nFace = nIdx / 3;

  if (nVert == 0 && nIdx == 0) {
    uint16_t* meta = (uint16_t*)vertices;

    CullingEngine::OccluderMesh raw;
    raw.QuadSafeBatchNum = meta[2];
    raw.TriangleBatchIdxNum = meta[3];

    int idxNum = raw.QuadSafeBatchNum * 16 + raw.TriangleBatchIdxNum * 12;
    int vertNum = meta[1];
    if (vertNum <= CullingEngine::SuperCompressVertNum) {
      idxNum >>= 1;
    }

    int vertSize = vertNum * 3;
    int total16 = vertSize + idxNum + 16;
    int line8 = total16 / 8;
    int left = (total16 & 7);

    uint16_t* data = (uint16_t*)vertices;
    fprintf(fptr, "CompactOccluder\n");
    fprintf(fptr, "%d\n", (int)(total16 + 7) >> 3);

    for (int i_vert = 0; i_vert < line8; ++i_vert, data += 8) {
      fprintf(fptr, "%hu %hu %hu %hu %hu %hu %hu %hu\n", data[0], data[1],
              data[2], data[3], data[4], data[5], data[6], data[7]);
    }
    if (left > 0) {
      for (int i_vert = 0; i_vert < left; ++i_vert, data++) {
        fprintf(fptr, "%hu ", data[0]);
      }
      for (int i_vert = left; i_vert < 8; ++i_vert) {
        fprintf(fptr, "0 ");
      }
      fprintf(fptr, "\n");
    }
  } else {
    fprintf(fptr, "Occluder\n");
    int backfaceCullOff = !backfaceCull;
    fprintf(fptr, "%d %d\n", (int)nVert + (backfaceCullOff * 10000000),
            (int)nFace);

    for (uint32_t i_vert = 0; i_vert < nVert; ++i_vert) {
      fprintf(fptr, "%f %f %f\n", vertices[i_vert * 3 + 0],
              vertices[i_vert * 3 + 1], vertices[i_vert * 3 + 2]);
    }

    for (uint32_t i_face = 0; i_face < nFace; ++i_face) {
      fprintf(fptr, "%d %d %d\n", indices[i_face * 3 + 0],
              indices[i_face * 3 + 1], indices[i_face * 3 + 2]);
    }
  }

  static constexpr float IdentityMatrix[16] = {
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  const float* pose = localToWorld != nullptr ? localToWorld : IdentityMatrix;
  for (int i = 0; i < 4; i++, pose += 4) {
    fprintf(fptr, "%f %f %f %f\n", pose[0], pose[1], pose[2], pose[3]);
  }
}
}  // namespace CullingEngine
