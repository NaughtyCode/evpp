#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "CullingEngineAPI.h"
#include "TestHelpers.h"

using CullingEngineTests::EngineHandle;
using CullingEngineTests::IdentityMatrix;
using CullingEngineTests::StartDefaultFrame;
using CullingEngineTests::VisibleBox;

namespace {
constexpr float kQuadVertices[] = {-0.75f, -0.75f, 1.0f, 0.75f,  -0.75f, 1.0f,
                                   0.75f,  0.75f,  1.0f, -0.75f, 0.75f,  1.0f};

constexpr float kOffscreenQuadVertices[] = {10.0f,  -0.75f, 1.0f,  11.0f,
                                            -0.75f, 1.0f,   11.0f, 0.75f,
                                            1.0f,   10.0f,  0.75f, 1.0f};

constexpr unsigned short kQuadIndices[] = {0, 1, 2, 0, 2, 3};

constexpr unsigned short kOutOfRangeIndices[] = {0, 1, 4};
}  // namespace

TEST_CASE("batch query validates inputs") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  auto box = VisibleBox();
  bool visible = false;

  CHECK_FALSE(CullingEngineQueryOccludees(nullptr, box.data(), 1, &visible));
  CHECK_FALSE(CullingEngineQueryOccludees(engine.Get(), nullptr, 1, &visible));
  CHECK_FALSE(
      CullingEngineQueryOccludees(engine.Get(), box.data(), 0, &visible));
  CHECK_FALSE(
      CullingEngineQueryOccludees(engine.Get(), box.data(), 1, nullptr));
}

TEST_CASE("empty scene conservatively reports occludees visible") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  std::array<float, 12> boxes{-0.25f, -0.25f, 1.0f, 0.25f, 0.25f, 2.0f,
                              -0.50f, -0.50f, 2.0f, 0.50f, 0.50f, 3.0f};
  bool results[2] = {false, false};

  REQUIRE(CullingEngineQueryOccludees(engine.Get(), boxes.data(), 2, results));
  CHECK(results[0]);
  CHECK(results[1]);
}

TEST_CASE("depth readback supports byte-aligned caller buffers") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  int widthHeight[2] = {};
  REQUIRE(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, widthHeight));
  const std::size_t pixelCount = static_cast<std::size_t>(widthHeight[0]) *
                                 static_cast<std::size_t>(widthHeight[1]);
  std::vector<unsigned char> storage(pixelCount * 2 + 1, 0xCD);
  unsigned char* unalignedOutput = storage.data() + 1;

  CHECK(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_DEPTH_MAP,
                          unalignedOutput));

  uint64_t metadataCount = 1;
  std::memcpy(&metadataCount, unalignedOutput + pixelCount,
              sizeof(metadataCount));
  CHECK(metadataCount == 0);
}

TEST_CASE("show-culled mode inverts query results") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  auto box = VisibleBox();
  bool visible = false;
  REQUIRE(CullingEngineQueryOccludees(engine.Get(), box.data(), 1, &visible));
  REQUIRE(visible);

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SHOW_CULLED, 1));
  visible = true;
  REQUIRE(CullingEngineQueryOccludees(engine.Get(), box.data(), 1, &visible));
  CHECK_FALSE(visible);

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SHOW_CULLED, 0));
  visible = false;
  REQUIRE(CullingEngineQueryOccludees(engine.Get(), box.data(), 1, &visible));
  CHECK(visible);
}

TEST_CASE(
    "before-query true-as-culled mode consumes preexisting true results") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_BEFORE_QUERY_TREAT_TRUE_AS_CULLED, 1));

  auto box = VisibleBox();
  bool visible = true;
  REQUIRE(CullingEngineQueryOccludees(engine.Get(), box.data(), 1, &visible));
  CHECK_FALSE(visible);
}

TEST_CASE("raw occluder submission tolerates invalid and valid meshes") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  auto identity = IdentityMatrix();
  CullingEngineRenderOccluder(nullptr, kQuadVertices, kQuadIndices, 4, 6,
                              identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), nullptr, kQuadIndices, 4, 6,
                              identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, nullptr, 4, 6,
                              identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kQuadIndices, 0, 6,
                              identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kQuadIndices, 4, 5,
                              identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kOutOfRangeIndices,
                              4, 3, identity.data(), false, true);
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kQuadIndices, 4, 6,
                              nullptr, false, true);

  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kQuadIndices, 4, 6,
                              identity.data(), false, true);
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER,
                         0));

  bool pvs[2] = {false, true};
  CHECK(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET, pvs));

  REQUIRE(StartDefaultFrame(engine.Get()));
  CHECK_FALSE(CullingEngineSet(engine.Get(),
                               CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS, 2));
  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS, 1));
}

TEST_CASE("new occluder submissions reset potential visibility state") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  auto identity = IdentityMatrix();
  CullingEngineRenderOccluder(engine.Get(), kOffscreenQuadVertices,
                              kQuadIndices, 4, 6, identity.data(), false, true);
  REQUIRE(CullingEngineSet(engine.Get(),
                           CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER, 0));

  bool previousPvs[2] = {true, true};
  REQUIRE(CullingEngineSync(engine.Get(),
                            CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET,
                            previousPvs));
  REQUIRE_FALSE(previousPvs[0]);

  REQUIRE(StartDefaultFrame(engine.Get()));
  CullingEngineRenderOccluder(engine.Get(), kQuadVertices, kQuadIndices, 4, 6,
                              identity.data(), false, true);

  bool currentPvs[2] = {false, false};
  REQUIRE(CullingEngineSync(engine.Get(),
                            CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET,
                            currentPvs));
  CHECK(currentPvs[0]);
}

TEST_CASE("query tree metadata is clamped to the submitted occludee count") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  uint16_t treeData[1] = {32};
  REQUIRE(CullingEngineSync(engine.Get(), CULLING_ENGINE_SET_QUERY_TREE_DATA,
                            treeData));

  std::array<float, 6> offscreenBox{10.0f, -0.25f, 1.0f, 11.0f, 0.25f, 2.0f};
  std::array<bool, 8> results;
  results.fill(true);

  REQUIRE(CullingEngineQueryOccludees(engine.Get(), offscreenBox.data(), 1,
                                      results.data()));
  CHECK_FALSE(results[0]);
  for (std::size_t i = 1; i < results.size(); ++i) {
    CHECK(results[i]);
  }
}

TEST_CASE("baked occluder submission reports rasterized triangle count") {
#if defined(CULLING_ENGINE_NATIVE)
  void* buffer = CullingEngineCreateOccluderBakeBuffer();
  REQUIRE(buffer != nullptr);

  int outputSize = 0;
  unsigned short* baked =
      CullingEngineMeshBake(buffer, &outputSize, kQuadVertices, kQuadIndices, 4,
                            6, 15.0f, true, true, 0);
  REQUIRE(baked != nullptr);
  REQUIRE(outputSize > 0);

  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
  REQUIRE(StartDefaultFrame(engine.Get()));

  auto identity = IdentityMatrix();
  int rasterizedTriangles = -1;
  CullingEngineRenderBakedOccluder(engine.Get(), baked, identity.data(), false,
                                   &rasterizedTriangles);
  CHECK(rasterizedTriangles >= 0);
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER,
                         0));

  CullingEngineRenderBakedOccluder(nullptr, baked, identity.data(), false,
                                   nullptr);
  CullingEngineRenderBakedOccluder(engine.Get(), nullptr, identity.data(),
                                   false, nullptr);
  CullingEngineRenderBakedOccluder(engine.Get(), baked, nullptr, false,
                                   nullptr);

  int invalidOutputSize = -1;
  CHECK(CullingEngineMeshBake(buffer, &invalidOutputSize, kQuadVertices,
                              kOutOfRangeIndices, 4, 3, 15.0f, true, true,
                              0) == nullptr);
  CHECK(invalidOutputSize == 0);

  CullingEngineDestroyOccluderBakeBuffer(buffer);
#endif
}
