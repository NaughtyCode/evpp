#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CullingEngineAPI.h"
#include "CullingEngineLog.h"
#include "TestHelpers.h"

using CullingEngineTests::EngineHandle;
using CullingEngineTests::MakeTempDirectory;
using CullingEngineTests::StartDefaultFrame;

namespace {
std::vector<std::string> gMessages;
std::vector<std::string> gFiles;
std::vector<int> gLines;

void CaptureLog(const char* msg, const char* filename, int line) {
  gMessages.emplace_back(msg != nullptr ? msg : "");
  gFiles.emplace_back(filename != nullptr ? filename : "");
  gLines.emplace_back(line);
}

std::string WithTrailingSeparator(const std::filesystem::path& path) {
  std::string value = path.string();
  const char preferred = std::filesystem::path::preferred_separator;
  if (!value.empty() && value.back() != '/' && value.back() != '\\') {
    value.push_back(preferred);
  }
  return value;
}

uint32_t ReadBigEndian32(const std::array<unsigned char, 24>& header,
                         std::size_t offset) {
  return (static_cast<uint32_t>(header[offset]) << 24) |
         (static_cast<uint32_t>(header[offset + 1]) << 16) |
         (static_cast<uint32_t>(header[offset + 2]) << 8) |
         static_cast<uint32_t>(header[offset + 3]);
}

bool IsPngWithDimensions(const std::filesystem::path& path, uint32_t width,
                         uint32_t height) {
  std::array<unsigned char, 24> header{};
  std::ifstream input(path, std::ios_base::binary);
  input.read(reinterpret_cast<char*>(header.data()),
             static_cast<std::streamsize>(header.size()));
  if (input.gcount() != static_cast<std::streamsize>(header.size())) {
    return false;
  }

  const std::array<unsigned char, 8> pngSignature{
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  for (std::size_t idx = 0; idx < pngSignature.size(); ++idx) {
    if (header[idx] != pngSignature[idx]) {
      return false;
    }
  }
  return ReadBigEndian32(header, 16) == width &&
         ReadBigEndian32(header, 20) == height;
}
}  // namespace

TEST_CASE("log callback receives formatted messages") {
  gMessages.clear();
  gFiles.clear();
  gLines.clear();

  CullingEngineSetLogFunc(CaptureLog);
  CullingEngine::LogFormatted(CullingEngine::Level::kWarning, "unit.cpp", 42,
                              "hello %d", 7);

  REQUIRE(gMessages.size() == 1);
  CHECK(gMessages[0] == "hello 7");
  CHECK(gFiles[0] == "unit.cpp");
  CHECK(gLines[0] == 42);

  CullingEngineSetLogFunc(nullptr);
}

TEST_CASE("preformatted logs use the callback path when storage is disabled") {
  gMessages.clear();
  gFiles.clear();
  gLines.clear();

  int disabled = 0;
  REQUIRE(CullingEngineSync(nullptr, CULLING_ENGINE_SET_PRINT_LOG_IN_GAME,
                            &disabled));
  CullingEngineSetLogFunc(CaptureLog);

  CullingEngine::AddLog("direct message", "direct.cpp", 9);

  REQUIRE(gMessages.size() == 1);
  CHECK(gMessages[0] == "direct message");
  CHECK(gFiles[0] == "direct.cpp");
  CHECK(gLines[0] == 9);

  CullingEngineSetLogFunc(nullptr);
}

TEST_CASE("stored log messages can be drained through sync") {
  CullingEngineSetLogFunc(nullptr);

  int enabled = 1;
  REQUIRE(CullingEngineSync(nullptr, CULLING_ENGINE_SET_PRINT_LOG_IN_GAME,
                            &enabled));
  CullingEngine::AddLog("stored message", __FILE__, __LINE__);

  char output[256] = {};
  CHECK(CullingEngineSync(nullptr, CULLING_ENGINE_GET_LOG, output));
  CHECK(std::string(output) == "stored message");
  CHECK_FALSE(CullingEngineSync(nullptr, CULLING_ENGINE_GET_LOG, output));

  enabled = 0;
  CHECK(CullingEngineSync(nullptr, CULLING_ENGINE_SET_PRINT_LOG_IN_GAME,
                          &enabled));
  CHECK_FALSE(CullingEngineSync(nullptr, CULLING_ENGINE_SET_PRINT_LOG_IN_GAME,
                                nullptr));
  CHECK_FALSE(CullingEngineSync(nullptr, CULLING_ENGINE_GET_LOG, nullptr));
  CHECK(CullingEngineSync(nullptr, CULLING_ENGINE_PRINT_LOG, nullptr));
}

TEST_CASE("capture path sync and frame capture update latest cap filename") {
  CullingEngineResetLatestCapFilename();
  CHECK_FALSE(CullingEngineIsValidLatestCapFilename());

  const auto captureDir = MakeTempDirectory("culling_engine_capture_test");
  std::string outputPath = WithTrailingSeparator(captureDir);

  REQUIRE(CullingEngineSync(nullptr,
                            CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH,
                            outputPath.data()));
  CHECK_FALSE(CullingEngineSync(
      nullptr, CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH, nullptr));

  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_CAPTURE_FRAME, 1));
  REQUIRE(StartDefaultFrame(engine.Get()));
  REQUIRE(StartDefaultFrame(engine.Get()));

  REQUIRE(CullingEngineIsValidLatestCapFilename());
  const char* latest = CullingEngineGetLatestCapFilename();
  REQUIRE(latest != nullptr);
  CHECK(std::filesystem::exists(latest));

  CullingEngineResetLatestCapFilename();
  CHECK_FALSE(CullingEngineIsValidLatestCapFilename());

  std::filesystem::remove_all(captureDir);
}

TEST_CASE("destroying an engine finalizes an active frame capture") {
  CullingEngineResetLatestCapFilename();

  const auto captureDir =
      MakeTempDirectory("culling_engine_capture_destroy_test");
  std::string outputPath = WithTrailingSeparator(captureDir);
  REQUIRE(CullingEngineSync(nullptr,
                            CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH,
                            outputPath.data()));

  void* engine = CullingEngineInit(64, 8, 1.0f);
  REQUIRE(engine != nullptr);

  CHECK(CullingEngineSet(engine, CULLING_ENGINE_CAPTURE_FRAME, 1));
  REQUIRE(StartDefaultFrame(engine));
  CHECK(CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 1));

  REQUIRE(CullingEngineIsValidLatestCapFilename());
  const char* latest = CullingEngineGetLatestCapFilename();
  REQUIRE(latest != nullptr);
  CHECK(std::filesystem::exists(latest));

  CullingEngineResetLatestCapFilename();
  std::filesystem::remove_all(captureDir);
}

TEST_CASE("occluder-occludee color image dump validates input") {
  constexpr unsigned int width = 4;
  constexpr unsigned int height = 2;
  constexpr std::size_t resolution = width * height;
  std::vector<unsigned char> storage(resolution + sizeof(uint64_t) + 1, 16);
  unsigned char* buffer = storage.data() + 1;
  const uint64_t metadataCount = 0;
  std::memcpy(buffer + resolution, &metadataCount, sizeof(metadataCount));

  CHECK_FALSE(CullingEngineDumpOccluderOccludeeColorImage(nullptr, buffer,
                                                          width, height));
  CHECK_FALSE(CullingEngineDumpOccluderOccludeeColorImage("unused.png", nullptr,
                                                          width, height));

#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)
  const auto dumpDir = MakeTempDirectory("culling_engine_dump_test");
  const auto dumpPath = dumpDir / "depth.png";

  CHECK(CullingEngineDumpOccluderOccludeeColorImage(dumpPath.string().c_str(),
                                                    buffer, width, height));
  CHECK(std::filesystem::exists(dumpPath));
  CHECK(IsPngWithDimensions(dumpPath, width, height));

  std::vector<unsigned char> tinyStorage(1 + sizeof(uint64_t), 0);
  CHECK_FALSE(CullingEngineDumpOccluderOccludeeColorImage(
      (dumpDir / "tiny.png").string().c_str(), tinyStorage.data(), 1, 1));

  std::filesystem::remove_all(dumpDir);
#else
  CHECK_FALSE(CullingEngineDumpOccluderOccludeeColorImage("unused.png", buffer,
                                                          width, height));
#endif
}

TEST_CASE("save depth map sync writes PNG and updates latest filename") {
#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)
  CullingEngineResetLatestCaptureDepthMapFilename();

  constexpr unsigned int width = 64;
  constexpr unsigned int height = 8;
  const auto dumpDir = MakeTempDirectory("culling_engine_depth_png_test");
  const auto dumpPath = dumpDir / "nested" / "depth.png";
  std::string dumpPathString = dumpPath.string();

  EngineHandle engine(width, height, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  std::vector<unsigned char> buffer(width * height * 2, 0);
  REQUIRE(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_DEPTH_MAP,
                            buffer.data()));
  REQUIRE(CullingEngineSync(engine.Get(), CULLING_ENGINE_SAVE_DEPTH_MAP_PATH,
                            dumpPathString.data()));

  CHECK(CullingEngineSync(engine.Get(), CULLING_ENGINE_SAVE_DEPTH_MAP,
                          buffer.data()));
  CHECK(IsPngWithDimensions(dumpPath, width, height));
  REQUIRE(CullingEngineIsValidLatestCaptureDepthMapFilename());
  CHECK(std::filesystem::path(CullingEngineGetLatestCaptureDepthMapFilename()) ==
        dumpPath);

  CullingEngineResetLatestCaptureDepthMapFilename();
  std::filesystem::remove_all(dumpDir);
#endif
}
