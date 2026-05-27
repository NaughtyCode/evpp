#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "CullingEngineAPI.h"
#include "CullingEngineLogger.h"
#include "CullingEngineReplayTest.h"
#include "OccluderManager.h"
// #include <Windows.h>

const char* GetTextForEnum(int enumVal) {
  for (int i = 24; i <= 26; i++) {
    if (enumVal & (1 << i)) {
      enumVal -= 1 << i;
    }
  }

  switch (enumVal) {
    case CullingEngine::AlgoEnum::kRasterizerFullTriangle:
      return "kRasterizerFullTriangle";

#ifdef CULLING_ENGINE_NATIVE_DEBUG

    case CullingEngine::AlgoEnum::kRasterizerFullTriangle2:
      return "kRasterizerFullTriangle2";
    case CullingEngine::AlgoEnum::kRasterizerFullTriangle3:
      return "kRasterizerFullTriangle3";
    case CullingEngine::AlgoEnum::kRasterizerFullTriangle4:
      return "kRasterizerFullTriangle4";
    case CullingEngine::AlgoEnum::kRasterizerFullTriangle5:
      return "kRasterizerFullTriangle5";

#endif
    default:
      break;
  }
  return "Undefined ";
}

bool SocReplay(const char* file_path, float* result, int config = 0,
               int frameNum = 0, uint64_t settingConfig = 0) {
  //	Sleep(3000);
  void* pCullingEngine = CullingEngineInit(1024, 512, 1.0f);
  if (pCullingEngine == nullptr) {
    return false;
  }
  if (!CullingEngineSync(pCullingEngine, 9999, result) ||
      !CullingEngineSync(pCullingEngine, 10000, &config) ||
      !CullingEngineSync(pCullingEngine, 10001, &frameNum) ||
      !CullingEngineSync(pCullingEngine, 10003, &settingConfig)) {
    CullingEngineSet(pCullingEngine, CULLING_ENGINE_DESTROY, 1);
    return false;
  }
  return CullingEngineSync(pCullingEngine, 10002, (void*)file_path);
}

static int frameCount = 500;
bool Test(const char* file_path, float* result, int config = 0,
          int frameNum = 0, uint64_t settingConfig = 0) {
#ifdef CULLING_ENGINE_NATIVE_DEBUG
  CULLING_ENGINE_LOG_INFO("Replay test algorithm: %s",
                          GetTextForEnum(config));

  bool output = SocReplay(file_path, result, config, frameNum, settingConfig);
  return output;
#else
  CULLING_ENGINE_LOG_ERROR(
      "Replay test requires CULLING_ENGINE_NATIVE_DEBUG");
#endif
  return false;
}
bool IsFileExist(std::string fileName) {
  std::ifstream ifile;
  ifile.open(fileName);
  if (ifile) {
    return true;
  } else {
    return false;
  }
}

float DecompressFloat(uint16_t depth) {
  const float bias = 3.9623753e+28f;  // 1.0f / floatCompressionBias

  union {
    uint32_t u;
    float f;
  } U = {uint32_t(depth) << 12};
  return (U.f * bias);
}
static bool verify = true;

static bool RunSmokeTest() {
  void* pCullingEngine = CullingEngineInit(64, 8, 0.001f);
  if (pCullingEngine == nullptr) {
    return false;
  }

  bool ok = true;
  ok = ok && std::fabs(CullingEngineGetNearPlane(pCullingEngine) -
                       CULLING_ENGINE_MIN_NEAR_PLANE) < 0.0001f;

  float cameraPos[3] = {0.0f, 0.0f, 0.0f};
  float cameraDir[3] = {0.0f, 0.0f, 1.0f};
  float viewProj[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

  ok = ok && CullingEngineStartNewFrame(pCullingEngine, cameraPos, cameraDir,
                                        viewProj, false);
  ok = ok && !CullingEngineStartNewFrame(nullptr, cameraPos, cameraDir,
                                         viewProj, false);
  ok = ok && !CullingEngineStartNewFrame(pCullingEngine, nullptr, cameraDir,
                                         viewProj, false);

  float bbox[6] = {-1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 2.0f};
  bool visible = false;
  ok = ok && CullingEngineQueryOccludees(pCullingEngine, bbox, 1, &visible);
  ok = ok && !CullingEngineQueryOccludees(pCullingEngine, nullptr, 1, &visible);
  ok = ok && !CullingEngineQueryOccludees(pCullingEngine, bbox, 0, &visible);
  ok = ok && !CullingEngineSet(nullptr, CULLING_ENGINE_RENDER_MODE,
                               CULLING_ENGINE_RENDER_MODE_FULL);
  ok = ok && CullingEngineSet(pCullingEngine, CULLING_ENGINE_RENDER_MODE,
                              CULLING_ENGINE_RENDER_MODE_FULL);
  ok = ok &&
       !CullingEngineSet(pCullingEngine, CULLING_ENGINE_RENDER_MODE,
                         CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE + 1);
  ok = ok && !CullingEngineSet(pCullingEngine, CULLING_ENGINE_SET_CCW, 2);
  ok = ok && !CullingEngineSet(pCullingEngine,
                               CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER, 2);

  int widthHeight[2] = {0, 0};
  ok = ok && CullingEngineSync(pCullingEngine,
                               CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT,
                               widthHeight);
  ok = ok && widthHeight[0] == 64 && widthHeight[1] == 8;
  ok = ok &&
       !CullingEngineSync(nullptr, CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT,
                          widthHeight);

  CullingEngineSet(pCullingEngine, CULLING_ENGINE_DESTROY, 1);
  return ok;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--smoke") {
    return RunSmokeTest() ? 0 : 1;
  }

  float lagestDelta = 0;
  for (int idx = 1; idx < 65535; idx++) {
    float d = DecompressFloat(idx) - DecompressFloat(idx - 1);
    if (d > lagestDelta) lagestDelta = d;
  }
  CULLING_ENGINE_LOG_DEBUG("Depth decompress largest delta: %f inverse=%f",
                           lagestDelta,
                      1 / lagestDelta);
  CULLING_ENGINE_LOG_DEBUG("Depth decompress check: depth=65535 value=%f",
                           DecompressFloat(65535));
  union w {
    int a;
    char b;
  } c;
  c.a = 1;
  if (c.b == 1)
    CULLING_ENGINE_LOG_DEBUG("Host endian: little");
  else
    CULLING_ENGINE_LOG_DEBUG("Host endian: big");

  frameCount = 1;

  std::vector<std::string> allCaptures;

  bool configManual = argc <= 1;

  std::string developerGoldData = "../../GOLDEN_DATA/";
#ifdef CULLING_ENGINE_PLATFORM_MACOS
  developerGoldData = "../../../GOLDEN_DATA/";
#ifdef __aarch64__
  developerGoldData = "../../../GOLDEN_DATA/";
#endif

#endif
  bool quickCompare = true;
  if (configManual) {
    if (quickCompare) {
      developerGoldData += "all//";
      allCaptures.push_back("XYZDegenerate.cap");
      allCaptures.push_back("SuntempSlope.cap");
      allCaptures.push_back("SuntempleWindowBug.cap");
      allCaptures.push_back("SuntempleView.cap");
      allCaptures.push_back("SunTempleUnhandledMirrorBug.cap");
      allCaptures.push_back("SuntempleStatue.cap");
      allCaptures.push_back("SuntemplePackNearClipBugUnrollPartialQuad.cap");
      allCaptures.push_back("SuntempleOccludeeNeedMaxClampBug.cap");
      allCaptures.push_back("SuntempleLargeSlope.cap");
      allCaptures.push_back("SuntempleInterleaveBug.cap");
      allCaptures.push_back("SuntempleFloorBug.cap");
      allCaptures.push_back("SuntempleCeil.cap");
      allCaptures.push_back("SuntempleBug2.cap");
      allCaptures.push_back("Suntemple1.cap");
      allCaptures.push_back("SunPlane.cap");
      allCaptures.push_back("SunNearClip.cap");
      allCaptures.push_back("SunInterleaveBug.cap");
      allCaptures.push_back("SunBug.cap");
      allCaptures.push_back("SunBoundaryWall.cap");
      allCaptures.push_back("QSceneFull.cap");

    } else {
      allCaptures.push_back("QSceneFull.cap");
    }

  } else {
    if (argc <= 1) {
      return RunSmokeTest() ? 0 : 1;
    }
    developerGoldData = "";
    allCaptures.push_back(std::string(argv[1]));
  }

  if (configManual && !allCaptures.empty() &&
      !IsFileExist(developerGoldData + allCaptures.front())) {
    CULLING_ENGINE_LOG_WARNING(
        "Golden capture data not found. Running smoke test instead.");
    return RunSmokeTest() ? 0 : 1;
  }

  bool runCompare = true;
  if (runCompare) {
    int round = 1;
#ifdef CULLING_ENGINE_STRESS_TEST
    round = 200000;
#endif
    std::vector<int> algos;
    if (quickCompare) {
#ifdef CULLING_ENGINE_PLATFORM_MACOS
      //    algos.push_back(2);
#else
#endif
      // algos.push_back(2);
      // algos.push_back(3);
      int compressMode = 0;
      // algos.push_back(compressMode * 16 + 3); //pure triangle approach
      int renderMode = 0;
      int interleave = 0;

      // algos.push_back( (interleave << 8) + renderMode * 2048 + compressMode *
      // 16 + 2);
      compressMode = 0;
      algos.push_back((interleave << 8) + renderMode * 2048 +
                      compressMode * 16 + 3);
      interleave = 2;
      algos.push_back((interleave << 8) + renderMode * 2048 +
                      compressMode * 16 + 4);
    }

    std::vector<std::string> allResults;
    std::vector<float> allTimes;
    float results[10] = {};
    int totalAlgo = (int)algos.size();

    for (uint64_t roundIdx = 0; roundIdx < round; roundIdx++) {
      uint64_t rIdx = 1;
      for (auto cap : allCaptures) {
        CULLING_ENGINE_LOG_INFO("Replay test round: %llu",
                            static_cast<unsigned long long>(rIdx));
        CULLING_ENGINE_LOG_INFO("Replay target: %s", cap.c_str());

        for (auto inputConfig : algos) {
          auto inputCap = developerGoldData + cap;
          CULLING_ENGINE_LOG_INFO("Replay input: %s", inputCap.c_str());
          if (quickCompare) {
            //	quickVerify(inputCap.c_str());
          }
          bool useReplayer = false;
#if !defined(CULLING_ENGINE_NATIVE_DEBUG)
          useReplayer = true;
#endif
          uint64_t replaySetting = 0;
          // CullingEngine settings ******************************************
          //  on off interleave mode, 2 is coherent fast, 1 is coherent

          uint64_t dumpDrawCall = 0;  // set to 1 to dump per draw depth map
          uint64_t focusDraw = -1;  // ignore all other draw calls, only submit
                                    // the selected draw calls

          int approach = inputConfig & 7;
          // focusDraw = 13;
          if (focusDraw >= 0) {
            dumpDrawCall = 0;
          }

          uint64_t compressMode = (inputConfig >> 4) & 1;
          uint64_t renderMode = (inputConfig >> 11) & 7;

          CULLING_ENGINE_LOG_INFO("Replay render mode: %llu",
                              static_cast<unsigned long long>(renderMode));
          uint64_t interleave = (inputConfig >> 8) & 3;
          ;  // on off interleave mode

          uint64_t CW = (inputConfig & 1024) / 1024;
          replaySetting = ((focusDraw + 1) << 16) | (CW << 60) |
                          (renderMode << 9) | (compressMode << 8) |
                          (dumpDrawCall << 3) | interleave;
          replaySetting |= rIdx << 32;
          if (useReplayer) {
            CULLING_ENGINE_LOG_INFO("Running Android demo replay");
            auto result = TestCullingEngine(developerGoldData, cap);
            CULLING_ENGINE_LOG_INFO("Android demo replay result: %s",
                                result.c_str());
            break;
          } else if (Test(inputCap.c_str(), results, 1 << approach, frameCount,
                          replaySetting)) {
            if (round == 1) {
              float currentTime = results[0];
              allTimes.push_back(currentTime);
              std::string output =
                  inputCap + " " + GetTextForEnum(1 << approach);
              output += "    Time " + std::to_string(results[0]) + " Query " +
                        std::to_string((int)results[1]) + "/" +
                        std::to_string((int)results[2]) + " occluderCulled " +
                        std::to_string((int)results[3]) + "/" +
                        std::to_string((int)results[4]);
              // stress memory test, no need to store the results

              allResults.push_back(output);
              if (approach <= 6) {
                CULLING_ENGINE_LOG_INFO(
                    "Replay comparison: baseline=%s ratio=%f",
                    GetTextForEnum(1 << (algos[0] & 7)),
                    allTimes[(allTimes.size() - 1) / totalAlgo * totalAlgo] /
                        currentTime);
              }
            }
          } else {
            CULLING_ENGINE_LOG_ERROR("Replay test failed");
            return -1;
          }
          if (dumpDrawCall > 0) {
            return 0;
          }
        }
      }
      if (quickCompare) {
        int idx = 0;
        for (auto s : allResults) {
          if (idx % totalAlgo == 0) {
            CULLING_ENGINE_LOG_INFO("Replay summary: %s", s.c_str());
          } else {
            float current = allTimes[idx];
            float imoc = allTimes[idx / totalAlgo * totalAlgo];
            CULLING_ENGINE_LOG_INFO("Replay summary: %s ratio=%f", s.c_str(),
                                    imoc / current);
          }
          idx++;
        }

        allResults.clear();
      }
    }
  }
  return 0;
}
