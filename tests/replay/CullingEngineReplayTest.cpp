#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iosfwd>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>

#include "CullingEngineAPI.h"
#else
#include "CullingEngineAPI.h"
#include "CullingEngineReplayTest.h"
#endif
#include "CullingEngineLogger.h"

struct OccludeeGroup {
  // for batch query
  struct OccludeeBatch {
    OccludeeBatch(uint32_t num) {
      Number = num;
      data.resize(static_cast<std::size_t>(num) * 6u);
    }
    std::vector<float> data;
    uint32_t Number = 0;
  };
  std::vector<OccludeeBatch*> Batches;

  void Clear() {
    for (auto b : Batches) delete b;
    Batches.clear();
  }
};
struct CapturedFrameData {
  float CameraPos[3];
  float CameraDir[3];
  float ViewProj[16];

  struct OccludeeGroup Occludees;

  void Clear() {
    // Camera pos
    for (unsigned int i = 0; i < 3; ++i) {
      CameraPos[i] = 0.0f;
    }

    // array
    for (unsigned int i = 0; i < 16; ++i) {
      ViewProj[i] = 0.0f;
    }

    Occludees.Clear();
  }
};

static const std::string CullingEngine_FILE_HEAD = "SOC";
static const std::string FB_SETTING_HEADER = "Framebuffer Settings";
static const std::string CAM_POS_HEADER = "Camera PosDir";
static const std::string VIEW_PROJ_HEADER = "View Projection Matrix";
static const std::string BATCHED_OCE_HEADER = "Batched Occludee";
static const int BBOX_STRIDE = 6;

static void* pCullingEngine = nullptr;

class CullingEngineLoader {
 public:
  int OccluderID = 0;

  struct OccluderData {
    int backfaceCull = 1;  // 0,1
    int occluderID = 0;
    OccluderData(int occId) { this->occluderID = occId; }
    const float* Vertices = nullptr;
    unsigned int VerticesNum = 0;
    const uint16_t* Indices = nullptr;
    unsigned int nIdx = 0;
    float localToWorld[16];

    unsigned int CompactSize;
    unsigned short* CompactData = nullptr;

    ~OccluderData() {
      if (Vertices != nullptr) delete[] Vertices;
      if (Indices != nullptr) delete[] Indices;
      if (CompactData != nullptr) delete[] CompactData;
    }

    void CompressModel() {
      if (CompactData != nullptr) return;

      int outputCompressSize = 0;
      auto initStartTime = std::chrono::high_resolution_clock::now();
      void* bakeBuffer = CullingEngineCreateOccluderBakeBuffer();
      unsigned short* output = CullingEngineMeshBake(
          bakeBuffer, &outputCompressSize, this->Vertices, this->Indices,
          this->VerticesNum, this->nIdx, 15, true, true, 0);
      if (output != nullptr && outputCompressSize > 0) {
        CompactData =
            new unsigned short[static_cast<std::size_t>(outputCompressSize)];
        memcpy(CompactData, output,
               static_cast<std::size_t>(outputCompressSize) * sizeof(short));
      }
      CullingEngineDestroyOccluderBakeBuffer(bakeBuffer);
      auto time = (int)std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::high_resolution_clock::now() - initStartTime)
                      .count();
      CULLING_ENGINE_LOG_DEBUG(
          "Replay bake time: %.3f ms, faces=%d, vertices=%d",
          (time * 1.0 / 1000), nIdx / 3, VerticesNum);
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
    float CameraPos[3];
    float CameraDir[3];
    // ViewProj
    float ViewProj[16];

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
      CULLING_ENGINE_LOG_DEBUG("Replay test occludee count: %u", nOccludee);
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
    CULLING_ENGINE_LOG_DEBUG("Replay test occludee count: %u", nOccludee);
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
    occ->CompactData =
        new unsigned short[static_cast<std::size_t>(n128) * 8u];
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
    float* Vertices = new float[static_cast<std::size_t>(nVert) * 3u];
    for (int i_vert = 0; i_vert < nVert; ++i_vert) {
      fin >> Vertices[i_vert * 3 + 0] >> Vertices[i_vert * 3 + 1] >>
          Vertices[i_vert * 3 + 2];
      std::getline(fin, line);
    }
    occ->Vertices = Vertices;

    uint16_t* Indices = new uint16_t[static_cast<std::size_t>(occ->nIdx)];
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
      CULLING_ENGINE_LOG_WARNING("Replay test capture path is empty");
      return false;
    }

    // open the file
    std::ifstream fin(file_path);
    if (!fin) {
      CULLING_ENGINE_LOG_WARNING("Failed to open replay test capture: %s",
                                 file_path.c_str());
      return false;
    }

    std::string line;

    ////// get QCAP
    // load width, height & near plane
    if (!GetHeader(fin, FB_SETTING_HEADER)) {
      CULLING_ENGINE_LOG_WARNING(
          "Replay test capture missing framebuffer header");
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

static int ReplayMaxFrame = 0;
static std::string InputFolderPath = "";
static bool SleepBetweenFrames = false;
static bool QuickImageDumpMode = false;
void ReplayFrame(CullingEngineLoader* dataProvider, int mode, int frameNum,
                 int saveFrameIdx) {
#if defined(CULLING_ENGINE_PLATFORM_ANDROID)
  // set thread affinity
  // common golden core: 4~6 in Snapdragon chips
  // attempt to bind to a golden core
  cpu_set_t mask;
  for (int i_cpu = 4; i_cpu <= 6; ++i_cpu) {
    CPU_ZERO(&mask);
    CPU_SET(i_cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == 0) {
      break;
    }
  }
#endif

  unsigned int width = dataProvider->Width;
  unsigned int height = dataProvider->Height;

  pCullingEngine = CullingEngineInit(width, height, 1.0f);
  CullingEngineSet(pCullingEngine, CULLING_ENGINE_SET_CCW, dataProvider->CCW);
  CULLING_ENGINE_LOG_INFO("Replay test capture winding CCW=%u",
                          dataProvider->CCW);

  // std::string outputPath = InputFolderPath + "/";
  // CullingEngineSync(pCullingEngine,
  // CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH, (void*)outputPath.c_str());

  int widthHeight[2];
  widthHeight[0] = width;
  widthHeight[1] = height;
  CullingEngineSync(
      pCullingEngine, CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT,
      widthHeight);  // verify CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT
  CullingEngineSync(
      pCullingEngine, CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT,
      widthHeight);  // verify CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT
  width = widthHeight[0];
  height = widthHeight[1];

  CullingEngineSet(pCullingEngine, CULLING_ENGINE_RENDER_MODE,
                   mode);  // set coherent mode
  CullingEngineSet(pCullingEngine, CULLING_ENGINE_SET_CCW,
                   true);  // treat as CCW

  std::vector<unsigned char> buffer;
  if (saveFrameIdx >= 0) {
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > (std::numeric_limits<std::size_t>::max() / 2u)) {
      CULLING_ENGINE_LOG_WARNING("Replay test image dimensions are too large");
      return;
    }
    buffer.resize(pixelCount * 2u);  // depth pixels followed by metadata.
  }

  int visibleNum = 0;
  int totalQuery = 0;
  int occluderSubmitted = 0;

  int allResultsLength = 1024;
  std::unique_ptr<bool[]> allResults =
      std::make_unique<bool[]>(allResultsLength);

  ReplayMaxFrame = frameNum;
  if (QuickImageDumpMode && ReplayMaxFrame > frameNum) {
    ReplayMaxFrame = frameNum;
  }

#if defined(__aarch64__)
#else
  CULLING_ENGINE_LOG_DEBUG("Replay test uses compressed mesh input on Armv7");
#endif
  auto end = std::chrono::high_resolution_clock::now();
  auto start = std::chrono::high_resolution_clock::now();

  int totalRenderTime = 0;
  for (int frameIdx = 0; frameIdx < ReplayMaxFrame; frameIdx++) {
    auto frame = dataProvider->frame;

    start = std::chrono::high_resolution_clock::now();
    // start new frame
    CullingEngineStartNewFrame(pCullingEngine, frame->CameraPos,
                               frame->CameraDir, frame->ViewProj, false);
    occluderSubmitted = 0;

    for (auto occluder : frame->Occluders) {
      // Test only: make armv7 cover compress mesh input, and armv8 normal mesh
      // input
#if defined(__aarch64__)
      CullingEngineRenderOccluder(pCullingEngine, occluder->Vertices,
                                  occluder->Indices, occluder->VerticesNum,
                                  occluder->nIdx, occluder->localToWorld, false,
                                  true);
#else
      CullingEngineRenderBakedOccluder(pCullingEngine, occluder->CompactData,
                                       occluder->localToWorld, false, nullptr);
#endif

      occluderSubmitted++;
    }

    int maxQueryNum = 0;
    for (auto& batch : frame->Occludees) {
      maxQueryNum += batch->Number;
    }
    if (maxQueryNum > allResultsLength) {
      allResultsLength = 2 * maxQueryNum;
      allResults = std::make_unique<bool[]>(allResultsLength);
    }

    visibleNum = 0;
    int totalQueryNum = 0;

    for (auto& batch : frame->Occludees) {
      if (batch->Number == 0) {
        continue;
      }

      const int resultOffset = totalQueryNum;
      totalQueryNum += batch->Number;
      CullingEngineQueryOccludees(pCullingEngine, batch->data.data(),
                                  batch->Number,
                                  allResults.get() + resultOffset);

      for (int idx = resultOffset; idx < totalQueryNum; idx++) {
        visibleNum += (int)(allResults[idx] == true);
      }
    }
    totalQuery = totalQueryNum;

    end = std::chrono::high_resolution_clock::now();
    totalRenderTime +=
        (int)std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count();

    if (frameIdx <= saveFrameIdx) {
      CullingEngineSync(pCullingEngine, CULLING_ENGINE_GET_DEPTH_MAP,
                        &buffer[0]);  // to get the occlusion depth map
    }

    if (SleepBetweenFrames) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(15));  // simulate, aim for 60fps
    }
  }

  if (saveFrameIdx >= 0) {
    // Able to save PNG to show occludee status.
    std::string file = InputFolderPath + "depthbuffer.png";
    const char* pChar = file.c_str();
    CullingEngineSync(pCullingEngine, CULLING_ENGINE_SAVE_DEPTH_MAP_PATH,
                      (void*)pChar);
    CullingEngineSync(pCullingEngine, CULLING_ENGINE_SAVE_DEPTH_MAP,
                      &buffer[0]);
  }

  CULLING_ENGINE_LOG_INFO(
      "Replay test result: occluders=%d visible=%d totalQueries=%d",
      occluderSubmitted, visibleNum, totalQuery);
  CULLING_ENGINE_LOG_INFO("Replay test total render time: %d us",
                          totalRenderTime);

  CullingEngineSet(pCullingEngine, CULLING_ENGINE_DESTROY, 1);
}
std::string TestCullingEngine(std::string folderPath, std::string capFileName) {
  InputFolderPath = folderPath;
  auto loader = std::make_unique<CullingEngineLoader>();
  if (!loader->Load(InputFolderPath + capFileName)) {
    return "Fail";
  }
  int totalFrame = 500;
  ReplayFrame(loader.get(), CULLING_ENGINE_RENDER_MODE_COHERENT, totalFrame,
              totalFrame);

  return "Success";
}

std::string TestCullingEngineQuick(std::string folderPath,
                                   std::string capFileName) {
  InputFolderPath = folderPath;
  auto dataProvider = std::make_unique<CullingEngineLoader>();
  if (!dataProvider->Load(InputFolderPath + capFileName)) {
    return "Fail";
  }

  int totalFrame = 500;
  QuickImageDumpMode = true;
  ReplayFrame(dataProvider.get(), CULLING_ENGINE_RENDER_MODE_COHERENT_FAST,
              totalFrame, totalFrame);
  QuickImageDumpMode = false;

  return "Success";
}

#ifdef __ANDROID__
extern "C" JNIEXPORT jstring JNICALL
Java_com_qualcomm_CullingEnginedemo_MainActivity_CallCullingEngineStressTestInJNI(
    JNIEnv* env, jobject /* this */) {
  SleepBetweenFrames = true;
  int totalRound = 99999;
  std::string result = "";
  for (int idx = 0; idx < totalRound; idx++) {
    result = TestCullingEngine(
        "/sdcard/Android/data/com.qualcomm.CullingEnginedemo/files/",
        "input.cap");
  }
  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_qualcomm_CullingEnginedemo_MainActivity_CallCullingEngineInJNIQuick(
    JNIEnv* env, jobject /* this */) {
  SleepBetweenFrames = false;
  std::string result = TestCullingEngineQuick(
      "/sdcard/Android/data/com.qualcomm.CullingEnginedemo/files/",
      "input.cap");

  return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_qualcomm_CullingEnginedemo_MainActivity_GetPackageArmVersion(
    JNIEnv* env, jobject /* this */) {
#if defined(__aarch64__)
  return env->NewStringUTF("ArmV8 Normal Mesh");
#else
  return env->NewStringUTF("ArmV7 Compress Mesh");
#endif
}

#endif
