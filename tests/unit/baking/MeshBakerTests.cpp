#include <doctest/doctest.h>

#include "CullingEngineAPI.h"
#include "CullingEngineMeshBaker.h"

namespace {
constexpr float kQuadVertices[] = {-1.0f, -1.0f, 1.0f, 1.0f,  -1.0f, 1.0f,
                                   1.0f,  1.0f,  1.0f, -1.0f, 1.0f,  1.0f};

constexpr unsigned short kQuadIndices[] = {0, 1, 2, 0, 2, 3};

constexpr unsigned short kOutOfRangeIndices[] = {0, 1, 4};
}  // namespace

TEST_CASE("mesh bake buffer can be created and destroyed") {
#if defined(CULLING_ENGINE_NATIVE)
  void* buffer = CullingEngineCreateOccluderBakeBuffer();
  REQUIRE(buffer != nullptr);
  CullingEngineDestroyOccluderBakeBuffer(buffer);
  CullingEngineDestroyOccluderBakeBuffer(nullptr);
#else
  CHECK(CullingEngineCreateOccluderBakeBuffer() == nullptr);
  CullingEngineDestroyOccluderBakeBuffer(nullptr);
#endif
}

TEST_CASE("mesh bake rejects invalid inputs") {
  int outputSize = 123;
  CHECK(CullingEngineMeshBake(nullptr, &outputSize, kQuadVertices, kQuadIndices,
                              4, 6, 15.0f, true, true, 0) == nullptr);
  CHECK(outputSize == 0);

#if defined(CULLING_ENGINE_NATIVE)
  void* buffer = CullingEngineCreateOccluderBakeBuffer();
  REQUIRE(buffer != nullptr);

  CHECK(CullingEngineMeshBake(buffer, nullptr, kQuadVertices, kQuadIndices, 4,
                              6, 15.0f, true, true, 0) == nullptr);

  outputSize = 123;
  CHECK(CullingEngineMeshBake(buffer, &outputSize, nullptr, kQuadIndices, 4, 6,
                              15.0f, true, true, 0) == nullptr);
  CHECK(outputSize == 0);

  outputSize = 123;
  CHECK(CullingEngineMeshBake(buffer, &outputSize, kQuadVertices, nullptr, 4, 6,
                              15.0f, true, true, 0) == nullptr);
  CHECK(outputSize == 0);

  outputSize = 123;
  CHECK(CullingEngineMeshBake(buffer, &outputSize, kQuadVertices, kQuadIndices,
                              0, 6, 15.0f, true, true, 0) == nullptr);
  CHECK(outputSize == 0);

  outputSize = 123;
  CHECK(CullingEngineMeshBake(buffer, &outputSize, kQuadVertices, kQuadIndices,
                              4, 5, 15.0f, true, true, 0) == nullptr);
  CHECK(outputSize == 0);

  outputSize = 123;
  CHECK(CullingEngineMeshBake(buffer, &outputSize, kQuadVertices,
                              kOutOfRangeIndices, 4, 3, 15.0f, true, true,
                              0) == nullptr);
  CHECK(outputSize == 0);

  CullingEngineDestroyOccluderBakeBuffer(buffer);
#endif
}

TEST_CASE("mesh bake produces compact data for a simple quad") {
#if defined(CULLING_ENGINE_NATIVE)
  void* buffer = CullingEngineCreateOccluderBakeBuffer();
  REQUIRE(buffer != nullptr);

  int outputSize = 0;
  unsigned short* baked =
      CullingEngineMeshBake(buffer, &outputSize, kQuadVertices, kQuadIndices, 4,
                            6, 15.0f, true, true, 0);
  REQUIRE(baked != nullptr);
  CHECK(outputSize > 0);
  CHECK((baked[0] & 1) == 1);

  CullingEngineDestroyOccluderBakeBuffer(buffer);
#endif
}

TEST_CASE("AutoMeshBaker owns its temporary bake buffer") {
#if defined(CULLING_ENGINE_NATIVE)
  int outputSize = 0;
  AutoMeshBaker baker(&outputSize, kQuadVertices, kQuadIndices, 4, 6, 15.0f,
                      true, true, 0, false);
  CHECK(baker.m_pOccluderBakeBuffer != nullptr);
  CHECK(baker.GetBakeOutputBuffer() != nullptr);
  CHECK(outputSize > 0);
#endif
}
