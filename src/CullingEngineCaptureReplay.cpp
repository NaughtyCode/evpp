#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "CullingEngineAPI.h"
#include "CullingEngineCore.h"
#include "CullingEngineImageDump.h"
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
#include <direct.h>
#pragma warning(disable : 4996)
#elif defined(CULLING_ENGINE_PLATFORM_ANDROID)
#include <sys/stat.h>
#endif

#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)

using CullingEngine::BBOX_STRIDE;
using CullingEngine::FB_SETTING_HEADER;
using CullingEngine::VIEW_PROJ_HEADER;

static int compressedCount = 0;
static bool saveCheckerBoardImage = true;

class CullingEngineLoader {
 public:
  int OccluderID = 0;

  struct OccluderData {
    uint32_t IsRowMajorModelWorldMat : 1;
    uint32_t backfaceCull : 1;  // 0,1

    int occluderID = 0;
    OccluderData(int occId) {
      backfaceCull = true;
      IsRowMajorModelWorldMat = false;
      this->occluderID = occId;
    }

    const float* Vertices = nullptr;
    unsigned int VerticesNum = 0;
    const uint16_t* Indices = nullptr;
    unsigned int nIdx = 0;
    float localToWorld[16] = {};

    unsigned int CompactSize = 0;
    short* CompactData = nullptr;

    ~OccluderData() {
      if (Vertices != nullptr) delete[] Vertices;
      if (Indices != nullptr) delete[] Indices;
      if (CompactData != nullptr) delete[] CompactData;
    }

    void CompressModel() {
      if (CompactData != nullptr) {
        return;
      }

      int compressSize = 0;
      AutoMeshBaker baker(&compressSize, Vertices, Indices, VerticesNum, nIdx,
                          15, true, true, 0, true);
      unsigned short* pBakeOutputBuffer = baker.GetBakeOutputBuffer();
      if (pBakeOutputBuffer != nullptr && compressSize > 0) {
        CompactData = new short[static_cast<std::size_t>(compressSize)];
        memcpy(CompactData, pBakeOutputBuffer,
               static_cast<std::size_t>(compressSize) * sizeof(short));
      }

      compressedCount++;
    }
  };

  // for batch query
  struct OccludeeBatch {
    std::vector<float> data;
    uint32_t Number = 0;
    void UpdateSize(unsigned int nOccludee) {
      this->Number = nOccludee;
      this->data.resize(static_cast<std::size_t>(nOccludee) * BBOX_STRIDE);
    }
  };

  struct CapturedFrameData {
    float CameraPos[3] = {};
    float CameraDir[3] = {};
    // ViewProj
    float ViewProj[16] = {};

    std::vector<OccluderData*> Occluders;
    std::vector<OccludeeBatch*> Occludees;
    ~CapturedFrameData() {
      for (int i = 0; i < Occluders.size(); i++) {
        OccluderData* occ = Occluders[i];
        delete occ;
      }

      for (int i = 0; i < Occludees.size(); i++) {
        OccludeeBatch* occ = Occludees[i];
        delete occ;
      }

      Occluders.clear();
      Occludees.clear();
    }
  };

  ~CullingEngineLoader() {
    if (frame != nullptr) {
      delete frame;
      frame = nullptr;
    }
  }

  void LoadMatrix(std::ifstream& fin, float* matrix) {
    std::string line;
    for (unsigned int row = 0; row < 4; ++row) {
      fin >> matrix[row * 4 + 0] >> matrix[row * 4 + 1] >>
          matrix[row * 4 + 2] >> matrix[row * 4 + 3];
      std::getline(fin, line);
    }
  }

  bool GetHeader(std::ifstream& fin, const std::string& header) {
    std::string line;
    while (std::getline(fin, line)) {
      if (line.length() > 1) {
        if (line.find(header) != std::string::npos) {
          return true;
        }
      }
    }
    return false;
  }

  void LoadBatchedOccludee(std::ifstream& fin,
                           std::vector<OccludeeBatch*>& batches) {
    std::string line;
    // get the number

    unsigned int nOccludee = 0;
    if (!(fin >> nOccludee) ||
        nOccludee >
            (std::numeric_limits<std::size_t>::max() / BBOX_STRIDE)) {
      std::getline(fin, line);
      return;
    }
    std::getline(fin, line);

    OccludeeBatch* batch = new OccludeeBatch();
    batches.push_back(batch);
    batch->UpdateSize(nOccludee);
    if (nOccludee == 0) {
      CULLING_ENGINE_LOG_DEBUG("Replay capture occludee count: %u",
                               nOccludee);
      return;
    }
    float* arr = batch->data.data();

    for (unsigned int i_box = 0; i_box < nOccludee;
         ++i_box, arr += BBOX_STRIDE) {
      fin >> arr[0] >> arr[1] >> arr[2];
      std::getline(fin, line);

      fin >> arr[3] >> arr[4] >> arr[5];
      std::getline(fin, line);
    }
    CULLING_ENGINE_LOG_DEBUG("Replay capture occludee count: %u", nOccludee);
  }

  CullingEngineLoader() { frame = new CapturedFrameData(); }

  void LoadCompactOccluder(std::ifstream& fin,
                           std::vector<OccluderData*>& occluders) {
    std::string line;
    int n128 = 0;
    if (!(fin >> n128) || n128 <= 0 ||
        static_cast<std::size_t>(n128) >
            (std::numeric_limits<std::size_t>::max() / 8u)) {
      std::getline(fin, line);
      return;
    }
    std::getline(fin, line);

    // std::cout << "compact line " << n128 << std::endl;
    OccluderData* occ = new OccluderData(this->OccluderID++);
    occluders.push_back(occ);
    occ->CompactData = new short[static_cast<std::size_t>(n128) * 8u];
    uint16_t* data = (uint16_t*)occ->CompactData;
    for (int i_vert = 0; i_vert < n128; ++i_vert, data += 8) {
      fin >> data[0] >> data[1] >> data[2] >> data[3] >> data[4] >> data[5] >>
          data[6] >> data[7];
      // for (int i = 0; i < 8; i++)
      //	std::cout << data[i] << " ";
      // std::cout << std::endl;
      std::getline(fin, line);
    }
    LoadMatrix(fin, occ->localToWorld);
    return;
  }

  void LoadOccluder(std::ifstream& fin, std::vector<OccluderData*>& occluders) {
    std::string line;
    // load number of vertices and number of faces
    int nVert = 0;
    int nFace = 0;
    if (!(fin >> nVert >> nFace)) {
      return;
    }

    const bool backfaceCull = nVert < 10000000;
    if (backfaceCull == false) {
      nVert -= 10000000;  // extract backface cull bit
    }
    if (nVert <= 0 || nFace <= 0 ||
        nFace > (std::numeric_limits<int>::max() / 3)) {
      std::getline(fin, line);
      return;
    }

    OccluderData* occ = new OccluderData(this->OccluderID++);
    occluders.push_back(occ);
    occ->backfaceCull = backfaceCull;

    std::getline(fin, line);
    occ->VerticesNum = nVert;
    occ->nIdx = nFace * 3;

    // allocation
    float* Vertices = new float[static_cast<size_t>(nVert) * 3];
    for (int i_vert = 0; i_vert < nVert; ++i_vert) {
      fin >> Vertices[i_vert * 3 + 0] >> Vertices[i_vert * 3 + 1] >>
          Vertices[i_vert * 3 + 2];
      std::getline(fin, line);
    }
    occ->Vertices = Vertices;

    uint16_t* Indices = new uint16_t[static_cast<size_t>(nFace) * 3];
    for (int i_face = 0; i_face < nFace; ++i_face) {
      int i_face3 = i_face * 3;
      fin >> Indices[i_face3 + 0] >> Indices[i_face3 + 1] >>
          Indices[i_face3 + 2];
      std::getline(fin, line);
    }
    occ->Indices = Indices;

    // load LocalToWorld Matrix
    LoadMatrix(fin, occ->localToWorld);
    occ->CompressModel();
  }

  bool Load(const std::string& file_path) {
    if (file_path.empty()) {
      CULLING_ENGINE_LOG_WARNING("Replay capture path is empty");
      return false;
    }

    // open the file
    std::ifstream fin(file_path);
    if (!fin) {
      CULLING_ENGINE_LOG_WARNING("Failed to open replay capture: %s",
                                 file_path.c_str());
      return false;
    }

    std::string line;

    ////// get QCAP
    // load width, height & near plane
    if (!GetHeader(fin, FB_SETTING_HEADER)) {
      CULLING_ENGINE_LOG_WARNING("Replay capture missing framebuffer header");
      return false;
    }
    std::stringstream info;

    // get framebuffer settings
    fin >> Width >> Height >> NearPlane;
    int CloseWise = Height > 1000000;
    Height -= CloseWise * 1000000;
    this->CCW = 1 ^ CloseWise;
    std::getline(fin, line);

    // read frame 1
    std::getline(fin, line);
    if (line.find("Frame") == std::string::npos) {
      return true;
    }

    // frame 1
    int SaveFrameIndex = 0;
    fin >> SaveFrameIndex;
    std::getline(fin, line);

    // Camera PosDir
    std::getline(fin, line);

    CapturedFrameData& f = *frame;
    fin >> f.CameraPos[0] >> f.CameraPos[1] >> f.CameraPos[2] >>
        f.CameraDir[0] >> f.CameraDir[1] >> f.CameraDir[2];
    std::getline(fin, line);

    // View-Proj matrix
    if (!GetHeader(fin, VIEW_PROJ_HEADER)) {
      return false;
    }
    LoadMatrix(fin, f.ViewProj);

    do {
      std::getline(fin, line);
      info.clear();
      info.str(line);

      if (line.find("CompactOccluder") != std::string::npos) {
        LoadCompactOccluder(fin, f.Occluders);
      } else if (line.find("Occluder") != std::string::npos) {
        LoadOccluder(fin, f.Occluders);
      } else if (line.find("Batched Occludee") != std::string::npos) {
        LoadBatchedOccludee(fin, f.Occludees);
      }
    } while (fin.eof() == false);

    return true;
  }

  // width, height & near plane
  uint32_t Width = 0;
  uint32_t Height = 0;
  uint32_t CCW = 1;
  float NearPlane = 0.0f;

  CapturedFrameData* frame = nullptr;
};

bool CullingEngine::CullingEnginePrivate::Replay(const char* file_path,
                                                 int config, int frameNum,
                                                 uint64_t replaySetting,
                                                 float* replayResult) {
  CULLING_ENGINE_LOG_INFO("Replay config: %d", config);

  if (file_path == nullptr || replayResult == nullptr) {
    return false;
  }

  // create CullingEngineLoader if needed
  std::unique_ptr<CullingEngineLoader> loader =
      std::make_unique<CullingEngineLoader>();

  // reset before replay
  // first load, then save
  if (!loader->Load(file_path)) {
    CULLING_ENGINE_LOG_WARNING("Failed to load replay capture: %s",
                               file_path);
    return false;
  }
  if (loader->frame == nullptr) {
    CULLING_ENGINE_LOG_WARNING("Replay capture frame data is missing");
    return false;
  }
  compressedCount = 0;

  this->Resize(loader->Width, loader->Height);
  SetConfig(CULLING_ENGINE_SET_CCW, loader->CCW);
  CULLING_ENGINE_LOG_INFO("Replay capture winding CCW=%u", loader->CCW);

  // disable frame skipper
  // if (replaySetting & 3)
  //{
  //	this->configPerformanceMode(CULLING_ENGINE_RENDER_MODE_COHERENT);
  // }
  // else {
  //	this->configPerformanceMode(CULLING_ENGINE_RENDER_MODE_FULL);
  // }
  this->ConfigPerformanceMode(replaySetting & 3);

  CULLING_ENGINE_LOG_INFO("Replay capture resolution: %u x %u", loader->Width,
                          loader->Height);

  this->SetAlgoApproach((CullingEngine::AlgoEnum)config);

  bool dumpPerDraw = (replaySetting & 8) > 0;
  CULLING_ENGINE_LOG_INFO("Replay setting: flags=%llu algorithm=%d",
                      static_cast<unsigned long long>(replaySetting),
                      this->algoApproachMask);

  int width = m_frameInfo->Width;
  int height = m_frameInfo->Height;
  const std::size_t imagePixels =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (imagePixels > (std::numeric_limits<std::size_t>::max() / 2u)) {
    CULLING_ENGINE_LOG_WARNING("Replay image dimensions are too large");
    return false;
  }
  std::unique_ptr<unsigned char[]> image =
      std::make_unique<unsigned char[]>(imagePixels * 2u);
  m_frameInfo->FrameCounter = 0;
  bool printResult = false;
  auto end = std::chrono::high_resolution_clock::now();
  auto start = std::chrono::high_resolution_clock::now();
  int replayFrameIdx = 0;
  bool printedBug = false;

  int ReplayMaxFrame = frameNum;

  std::unique_ptr<unsigned char[]> blockMasks =
      std::make_unique<unsigned char[]>(512 * 1024);

  int allResultsLength = 1024;
  std::unique_ptr<bool[]> allResults =
      std::make_unique<bool[]>(allResultsLength);

  auto initStartTime = std::chrono::high_resolution_clock::now();
  int visibleNum = 0;
  int totalQueryNum = 0;
  int totalOccluderNum = 0;

  int compressMode = (replaySetting >> 8) & 1;
  int renderMode = (replaySetting >> 9) & 7;
  this->SetRenderType(renderMode);

  int roundNum = 65535 & (replaySetting >> 32);
  int focusDraw = (replaySetting << 32 >> 48) - 1;
  int loopCount = -1;

  start = std::chrono::high_resolution_clock::now();

  std::unique_ptr<bool[]> occluderStates[2];
  occluderStates[0] =
      std::make_unique<bool[]>(loader->frame->Occluders.size() + 1);
  memset(occluderStates[0].get(), 0,
         sizeof(bool) * (loader->frame->Occluders.size() + 1));
  occluderStates[1] =
      std::make_unique<bool[]>(loader->frame->Occluders.size() + 1);
  memset(occluderStates[1].get(), 0,
         sizeof(bool) * (loader->frame->Occluders.size() + 1));

  while (m_frameInfo->FrameCounter < ReplayMaxFrame) {
    loopCount++;

    CullingEngineLoader::CapturedFrameData* frame = loader->frame;

    // start new frame
    float* c = frame->CameraDir;
    if (c[0] == 0 && c[1] == 0 && c[2] == 0) {
      float p[3];
      p[0] = 1.0f;
      p[1] = p[2] = 0.0f;
      StartNewFrame(frame->CameraPos, p, frame->ViewProj);
    } else {
      StartNewFrame(frame->CameraPos, frame->CameraDir, frame->ViewProj);
    }
    // submit occluder

    if (loopCount > 0) {
      m_rapidRasterizer->UsePrevFrameOccluders((int)frame->Occluders.size());
      m_rapidRasterizer->FlushCachedOccluder(
          m_rapidRasterizer->mOccluderCenter->mCurrentOccNum);
    } else {
      // int count = 0;

      int maxAllow = 9999;

      if (dumpPerDraw) {
        if (frame->Occluders.size() <= loopCount) {
          return true;
        }
      }
      totalOccluderNum = 0;
      int drawIdx = 0;

      for (int occIdx = 0; occIdx < frame->Occluders.size(); occIdx++) {
        // std::cout << "occIdx " << occIdx << "  size " <<
        // frame->Occluders.size() << std::endl;
        auto occ = frame->Occluders[occIdx];
        bool submitDraw = true;
        if (dumpPerDraw) {
          if (drawIdx > loopCount) {
            submitDraw = false;
          }
        }

        if (focusDraw >= 0) {
          if (drawIdx != focusDraw) {
            submitDraw = false;
          }
        }

        // m_rapidRasterizer->EnableBackFaceCull(true);

        if (submitDraw) {
          if ((compressMode == false) || (occ->CompactData == nullptr)) {
            {
              if (occ->Indices == nullptr) {
                this->m_frameInfo->SubmitOccluder(
                    (float*)(occ->CompactData), nullptr, 0, 0,
                    occ->localToWorld, occ->IsRowMajorModelWorldMat, true);
              } else {
                this->m_frameInfo->SubmitOccluder(
                    occ->Vertices, occ->Indices, occ->VerticesNum, occ->nIdx,
                    occ->localToWorld, occ->IsRowMajorModelWorldMat,
                    occ->backfaceCull);

                // this->m_frameInfo->submitOccluder(occ->Vertices, indices,
                // occ->VerticesNum, 12, occ->modelAABB, occ->localToWorld,
                // occ->backfaceCull);
              }
            }
          } else {
            this->m_frameInfo->SubmitOccluder(
                (float*)(occ->CompactData), nullptr, 0, 0, occ->localToWorld,
                occ->IsRowMajorModelWorldMat, true);
          }
          totalOccluderNum++;
          m_rapidRasterizer->FlushCachedOccluder(
              m_rapidRasterizer->mOccluderCenter->mCurrentOccNum);
        }

        drawIdx++;

        maxAllow--;
        if (maxAllow == 0) {
          break;
        }
      }
    }

    {
      m_rapidRasterizer->SyncOccluderPVS(
          occluderStates[m_frameInfo->FrameCounter & 1].get());
    }

    if (totalQueryNum > allResultsLength) {
      allResultsLength = 2 * totalQueryNum;
      allResults = std::make_unique<bool[]>(allResultsLength);
    }

    totalQueryNum = 0;
    visibleNum = 0;

    for (auto& batch : frame->Occludees) {
      if (batch->Number == 0) {
        continue;
      }

      int resultOffset = totalQueryNum;
      totalQueryNum += batch->Number;
      if (totalQueryNum > allResultsLength) {
        allResultsLength = 2 * totalQueryNum;
        allResults = std::make_unique<bool[]>(allResultsLength);
      }
      BatchQuery(batch->data.data(), batch->Number,
                 allResults.get() + resultOffset);

      for (int idx = resultOffset; idx < totalQueryNum; idx++) {
        visibleNum += (int)(allResults[idx] == true);
        // std::cout << "Idx " << idx << " visible " << visibleNum << std::endl;
      }
    }

    replayResult[1] = (float)visibleNum;
    replayResult[2] = (float)totalQueryNum;

    if (printResult) {
      CULLING_ENGINE_LOG_DEBUG("Replay query result: visible=%d",
                               visibleNum);
      std::ostringstream resultStream;
      int i = 0;
      for (; i + 4 <= totalQueryNum; i += 4) {
        int r1 = allResults[i];
        int r2 = allResults[i + 1];
        int r3 = allResults[i + 2];
        int r4 = allResults[i + 3];
        int result = (r1 << 3) + (r2 << 2) + (r3 << 1) + r4;

        resultStream << std::hex << result;
      }
      for (; i < totalQueryNum; i++) {
        resultStream << (allResults[i] ? "1" : "0");
      }
      const std::string resultMask = resultStream.str();
      CULLING_ENGINE_LOG_DEBUG(
          "Replay result mask: frame=%llu value=%s",
          static_cast<unsigned long long>(m_frameInfo->FrameCounter),
          resultMask.c_str());
    }

    if (dumpPerDraw) {
      if (focusDraw >= 0 && loopCount != focusDraw) {
        continue;
      }

      DoDumpDepthMap(image.get(), CullingEngine::DumpImageMode::kDumpFull);
      std::string inputFile = std::string(file_path);
      std::string result = inputFile.substr(0, inputFile.length() - 4) + "_" +
                           std::to_string(config) + "_" +
                           std::to_string(loopCount) + ".png";
      DumpOccluderOccludeeColorImage(result, image.get(), width, height);
    }

    if (loopCount == 5 && false) {
      std::string inputFile = std::string(file_path);

      std::size_t found = inputFile.find_last_of("/");
      std::string fileName = inputFile.substr(found + 1);
      m_frameInfo->mOutputSaveCap = fileName;
      OnFrameCaptureSet(1);
    }
  }

  replayResult[3] = 0;
  {
    int culled = 0;
    for (int idx = 0; idx < loader->frame->Occluders.size(); idx++) {
      culled += !occluderStates[0][idx] && !occluderStates[1][idx];
    }
    CULLING_ENGINE_LOG_INFO("Replay occluders culled: %d", culled);
    replayResult[3] = (float)culled;
  }

  replayResult[4] = (float)loader->frame->Occluders.size();
  end = std::chrono::high_resolution_clock::now();
  int time =
      (int)std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();

  CULLING_ENGINE_LOG_INFO("Replay render time: %d us", time);

  CULLING_ENGINE_LOG_INFO(
      "Replay last frame: visible=%d totalQueries=%d totalOccluders=%d",
      visibleNum, totalQueryNum, totalOccluderNum);
  // token file_path to get the capture name
  std::string inputFile = std::string(file_path);

  std::size_t found = inputFile.find_last_of("/");
  std::string fileName = inputFile.substr(found + 1);
  std::string folderName = inputFile.substr(0, found + 1) + "/Output/";

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
  if (!CullingEngineFrameCapture::CreateDirectories(folderName)) {
    CULLING_ENGINE_LOG_WARNING("Failed to create replay output directory: %s",
                        folderName.c_str());
    return false;
  }
#elif defined(CULLING_ENGINE_PLATFORM_ANDROID)
  if (!CullingEngineFrameCapture::CreateDirectories(folderName)) {
    CULLING_ENGINE_LOG_WARNING("Failed to create replay output directory: %s",
                        folderName.c_str());
    return false;
  }
#endif

  time = (int)std::chrono::duration_cast<std::chrono::microseconds>(
             end - initStartTime)
             .count();
  CULLING_ENGINE_LOG_INFO("Replay total time: %f seconds",
                          time * 1.0 / 1000000);

  replayResult[0] = (float)(time * 1.0 / 1000000);

  // if(false)
  {
    DoDumpDepthMap(blockMasks.get(),
                   CullingEngine::DumpImageMode::kDumpBlockMask);
    DumpGrayImage(
        folderName + "depth" + std::to_string(config) + "_BlockMask.png",
        blockMasks.get(), 512, 1024);
    const std::string blockMaskPath =
        folderName + "depth" + std::to_string(config) + "_BlockMask.png";
    CULLING_ENGINE_LOG_INFO("Saved replay block mask PNG: %s",
                            blockMaskPath.c_str());
  }

  {
    DoDumpDepthMap(image.get(), CullingEngine::DumpImageMode::kDumpFull);
    // clean up the memory related...
    std::string result = inputFile.substr(0, inputFile.length() - 4) + "_" +
                         std::to_string(config) + "_r" +
                         std::to_string(roundNum) + ".png";
    DumpOccluderOccludeeColorImage(result, image.get(), width, height);
    CULLING_ENGINE_LOG_INFO("Saved replay depth PNG: %s", result.c_str());

    if (saveCheckerBoardImage) {
      saveCheckerBoardImage = false;
      int w = 256;
      std::unique_ptr<uint8_t[]> cbi = std::make_unique<uint8_t[]>(w * w);

      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardBlackPattern);
      DumpGrayImage(folderName + "kCheckerboardBlackPattern.png", cbi.get(), w,
                    w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardWhitePattern);
      DumpGrayImage(folderName + "kCheckerboardWhitePattern.png", cbi.get(), w,
                    w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardOddColumn);
      DumpGrayImage(folderName + "kCheckerboardOddColumn.png", cbi.get(), w, w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardEvenColumn);
      DumpGrayImage(folderName + "kCheckerboardEvenColumn.png", cbi.get(), w,
                    w);

      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardOddBlack);
      DumpGrayImage(folderName + "kCheckerboardOddBlack.png", cbi.get(), w, w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardEvenBlack);
      DumpGrayImage(folderName + "kCheckerboardEvenBlack.png", cbi.get(), w, w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardEvenWhite);
      DumpGrayImage(folderName + "kCheckerboardEvenWhite.png", cbi.get(), w, w);
      DoDumpDepthMap(cbi.get(),
                     CullingEngine::DumpImageMode::kCheckerboardOddWhite);
      DumpGrayImage(folderName + "kCheckerboardOddWhite.png", cbi.get(), w, w);
    }
  }

  CULLING_ENGINE_LOG_INFO("Replay output resolution: %d x %d", width, height);
  if (false) {
    for (int idx = CullingEngine::DumpImageMode::kDumpHiz;
         idx <= CullingEngine::DumpImageMode::kDumpBlockMask; idx++) {
      DoDumpDepthMap(image.get(), (CullingEngine::DumpImageMode)idx);

      if (idx == CullingEngine::DumpImageMode::kDumpHiz) {
        DumpGrayImage(folderName + "depth" + std::to_string(config) + "Hiz.png",
                      image.get(), width / 8, height / 8);
      }
    }
  }

  CULLING_ENGINE_LOG_INFO("Replay memory used: %zu bytes",
                          this->GetMemorySizeInBytes());
  return true;
}

#else  // Else of CULLING_ENGINE_NATIVE_DEBUG && CULLING_ENGINE_NATIVE
bool CullingEngine::CullingEnginePrivate::Replay(const char* file_path,
                                                 int config, int frameNum,
                                                 uint64_t replaySetting,
                                                 float* replayResult) {
  (void)file_path;
  (void)config;
  (void)frameNum;
  (void)replaySetting;
  (void)replayResult;
  return false;
}

#endif  // End of CULLING_ENGINE_NATIVE_DEBUG && CULLING_ENGINE_NATIVE
