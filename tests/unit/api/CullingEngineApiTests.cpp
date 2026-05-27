#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <type_traits>

#include "CullingEngineAPI.h"
#include "MathUtility.h"
#include "TestHelpers.h"

using CullingEngineTests::CameraDirection;
using CullingEngineTests::CameraPosition;
using CullingEngineTests::EngineHandle;
using CullingEngineTests::IdentityMatrix;

static_assert(
    std::is_same_v<PFN_CullingEngineInit, decltype(&CullingEngineInit)>);
static_assert(std::is_same_v<PFN_CullingEngineStartNewFrame,
                             decltype(&CullingEngineStartNewFrame)>);
static_assert(
    std::is_same_v<PFN_CullingEngineSet, decltype(&CullingEngineSet)>);
static_assert(
    std::is_same_v<PFN_CullingEngineSync, decltype(&CullingEngineSync)>);
static_assert(std::is_same_v<PFN_CullingEngineRenderOccluder,
                             decltype(&CullingEngineRenderOccluder)>);
static_assert(std::is_same_v<PFN_CullingEngineRenderBakedOccluder,
                             decltype(&CullingEngineRenderBakedOccluder)>);
static_assert(std::is_same_v<PFN_CullingEngineQueryOccludees,
                             decltype(&CullingEngineQueryOccludees)>);

namespace {
int gAllocatorMallocCount = 0;
int gAllocatorFreeCount = 0;

void* TrackingMalloc(size_t size) {
  ++gAllocatorMallocCount;
  return std::malloc(size);
}

void* TrackingFree(void* ptr) {
  ++gAllocatorFreeCount;
  std::free(ptr);
  return nullptr;
}
}  // namespace

TEST_CASE("CullingEngineInit validates framebuffer dimensions") {
  CHECK(CullingEngineInit(0, 8, 1.0f) == nullptr);
  CHECK(CullingEngineInit(63, 8, 1.0f) == nullptr);
  CHECK(CullingEngineInit(64, 7, 1.0f) == nullptr);
  CHECK(CullingEngineInit(65, 8, 1.0f) == nullptr);
  CHECK(CullingEngineInit(64, 9, 1.0f) == nullptr);
  CHECK(CullingEngineInit(65536, 8, 1.0f) == nullptr);
  CHECK(CullingEngineInit(64, 65536, 1.0f) == nullptr);

  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);
}

TEST_CASE("near plane API clamps invalid values") {
  CHECK(CullingEngineGetNearPlane(nullptr) ==
        doctest::Approx(CULLING_ENGINE_MIN_NEAR_PLANE));

  EngineHandle engine(64, 8, 0.001f);
  REQUIRE(engine);
  CHECK(CullingEngineGetNearPlane(engine.Get()) ==
        doctest::Approx(CULLING_ENGINE_MIN_NEAR_PLANE));

  CullingEngineSetNearPlane(engine.Get(), 4.25f);
  CHECK(CullingEngineGetNearPlane(engine.Get()) == doctest::Approx(4.25f));

  CullingEngineSetNearPlane(engine.Get(), -3.0f);
  CHECK(CullingEngineGetNearPlane(engine.Get()) ==
        doctest::Approx(CULLING_ENGINE_MIN_NEAR_PLANE));

  CullingEngineSetNearPlane(nullptr, 10.0f);
}

TEST_CASE("sync exposes resolution, memory, and version metadata") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);

  int widthHeight[2] = {};
  CHECK(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, widthHeight));
  CHECK(widthHeight[0] == 64);
  CHECK(widthHeight[1] == 8);

  CHECK_FALSE(CullingEngineSync(
      nullptr, CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, widthHeight));
  CHECK_FALSE(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, nullptr));

  unsigned int resized[2] = {128, 16};
  CHECK(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT, resized));
  CHECK(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, widthHeight));
  CHECK(widthHeight[0] == 128);
  CHECK(widthHeight[1] == 16);

  CHECK_FALSE(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT, resized));

  unsigned int invalidResize[2] = {32, 16};
  CHECK_FALSE(CullingEngineSync(engine.Get(),
                                CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT,
                                invalidResize));

  unsigned int invalidWidthAlignment[2] = {72, 16};
  CHECK_FALSE(CullingEngineSync(engine.Get(),
                                CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT,
                                invalidWidthAlignment));

  unsigned int invalidHeightAlignment[2] = {128, 18};
  CHECK_FALSE(CullingEngineSync(engine.Get(),
                                CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT,
                                invalidHeightAlignment));

  CHECK(CullingEngineSync(
      engine.Get(), CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT, widthHeight));
  CHECK(widthHeight[0] == 128);
  CHECK(widthHeight[1] == 16);

  int memoryKiB = 0;
  CHECK(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_MEMORY_USED,
                          &memoryKiB));
  CHECK(memoryKiB > 0);
  CHECK(CullingEngineGetMemorySizeInBytes(engine.Get()) > 0);
  CHECK(CullingEngineGetMemorySizeInBytes(nullptr) == 0);

  int version = 0;
  CHECK(CullingEngineSync(nullptr, CULLING_ENGINE_GET_VERSION, &version));
  CHECK(version ==
        CullingEngine::VERSION_MAJOR * 10 + CullingEngine::VERSION_SUB);
  CHECK(CullingEngineSync(nullptr, CULLING_ENGINE_GET_VERSION, nullptr));
}

TEST_CASE(
    "frame lifecycle validates required pointers and reports same camera "
    "state") {
  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);

  auto cameraPosition = CameraPosition();
  auto cameraDirection = CameraDirection();
  auto viewProjection = IdentityMatrix();

  CHECK_FALSE(CullingEngineStartNewFrame(nullptr, cameraPosition.data(),
                                         cameraDirection.data(),
                                         viewProjection.data(), false));
  CHECK_FALSE(CullingEngineStartNewFrame(engine.Get(), nullptr,
                                         cameraDirection.data(),
                                         viewProjection.data(), false));
  CHECK_FALSE(CullingEngineStartNewFrame(engine.Get(), cameraPosition.data(),
                                         nullptr, viewProjection.data(),
                                         false));
  CHECK_FALSE(CullingEngineStartNewFrame(engine.Get(), cameraPosition.data(),
                                         cameraDirection.data(), nullptr,
                                         false));

  bool sameCamera = true;
  CHECK(CullingEngineStartNewFrame(engine.Get(), cameraPosition.data(),
                                   cameraDirection.data(),
                                   viewProjection.data(), false));
  CHECK(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_IS_SAME_CAMERA,
                          &sameCamera));
  CHECK_FALSE(sameCamera);

  CHECK(CullingEngineStartNewFrame(engine.Get(), cameraPosition.data(),
                                   cameraDirection.data(),
                                   viewProjection.data(), false));
  CHECK(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_IS_SAME_CAMERA,
                          &sameCamera));
  CHECK(sameCamera);

  CHECK_FALSE(CullingEngineSync(engine.Get(), CULLING_ENGINE_GET_IS_SAME_CAMERA,
                                nullptr));
  CHECK_FALSE(CullingEngineSync(nullptr, CULLING_ENGINE_GET_IS_SAME_CAMERA,
                                &sameCamera));
}

TEST_CASE(
    "configuration API accepts documented values and rejects invalid values") {
  CHECK_FALSE(CullingEngineSet(nullptr, CULLING_ENGINE_RENDER_MODE,
                               CULLING_ENGINE_RENDER_MODE_FULL));

  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS, 0));
  CHECK_FALSE(CullingEngineSet(engine.Get(),
                               CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS, 1));

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_RENDER_MODE,
                         CULLING_ENGINE_RENDER_MODE_FULL));
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_RENDER_MODE,
                         CULLING_ENGINE_RENDER_MODE_COHERENT));
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_RENDER_MODE,
                         CULLING_ENGINE_RENDER_MODE_COHERENT_FAST));
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_RENDER_MODE,
                         CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE));
  CHECK_FALSE(
      CullingEngineSet(engine.Get(), CULLING_ENGINE_RENDER_MODE,
                       CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE + 1));

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SET_CCW, 0));
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SET_CCW, 1));
  CHECK_FALSE(CullingEngineSet(engine.Get(), CULLING_ENGINE_SET_CCW, 2));

  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SHOW_CULLED, 0));
  CHECK(CullingEngineSet(engine.Get(), CULLING_ENGINE_SHOW_CULLED, 1));
  CHECK_FALSE(CullingEngineSet(engine.Get(), CULLING_ENGINE_SHOW_CULLED, 2));

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP, 0));
  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP, 1));
  CHECK_FALSE(CullingEngineSet(engine.Get(),
                               CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP, 2));

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST, 0));
  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST, 1));
  CHECK_FALSE(CullingEngineSet(
      engine.Get(), CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST, 2));

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE, 0));
  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE, 1));
  CHECK_FALSE(CullingEngineSet(
      engine.Get(), CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE, 2));

  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER, 0));
  CHECK(CullingEngineSet(engine.Get(),
                         CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER, 1));
  CHECK_FALSE(CullingEngineSet(engine.Get(),
                               CULLING_ENGINE_DEBUG_PRINT_ACTIVE_OCCLUDER, 2));

  CHECK_FALSE(CullingEngineSet(engine.Get(), 0xFFFFFFFFu, 0));
}

TEST_CASE("custom allocator is paired for the opaque engine handle") {
  gAllocatorMallocCount = 0;
  gAllocatorFreeCount = 0;

  CHECK(CullingEngineInit(64, 8, 1.0f, TrackingMalloc, nullptr) == nullptr);
  CHECK(gAllocatorMallocCount == 0);
  CHECK(gAllocatorFreeCount == 0);

  void* invalid = CullingEngineInit(65, 8, 1.0f, TrackingMalloc, TrackingFree);
  CHECK(invalid == nullptr);
  CHECK(gAllocatorMallocCount == 1);
  CHECK(gAllocatorFreeCount == 1);

  void* engine = CullingEngineInit(64, 8, 1.0f, TrackingMalloc, TrackingFree);
  REQUIRE(engine != nullptr);
  CHECK(gAllocatorMallocCount == 2);
  CHECK(gAllocatorFreeCount == 1);

  CHECK(CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 1));
  CHECK(gAllocatorFreeCount == 2);
}

TEST_CASE("destroy command requires an explicit destroy value") {
  void* engine = CullingEngineInit(64, 8, 1.0f);
  REQUIRE(engine != nullptr);

  CHECK_FALSE(CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 0));
  CHECK(CullingEngineGetMemorySizeInBytes(engine) > 0);
  CHECK(CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 1));
}

TEST_CASE("frustum-check flag is toggled per engine") {
  CHECK_FALSE(CullingEngineGetIsNeedCheckInFrustum(nullptr));
  CullingEngineSetIsNeedCheckInFrustum(nullptr, true);

  EngineHandle engine(64, 8, 1.0f);
  REQUIRE(engine);

  CHECK(CullingEngineGetIsNeedCheckInFrustum(engine.Get()));
  CullingEngineSetIsNeedCheckInFrustum(engine.Get(), false);
  CHECK_FALSE(CullingEngineGetIsNeedCheckInFrustum(engine.Get()));
  CullingEngineSetIsNeedCheckInFrustum(engine.Get(), true);
  CHECK(CullingEngineGetIsNeedCheckInFrustum(engine.Get()));
}

TEST_CASE("latest capture filename state can be reset") {
  CullingEngineResetLatestCaptureDepthMapFilename();
  CullingEngineResetLatestCapFilename();

  CHECK_FALSE(CullingEngineIsValidLatestCaptureDepthMapFilename());
  CHECK(CullingEngineGetLatestCaptureDepthMapFilename() == nullptr);

  CHECK_FALSE(CullingEngineIsValidLatestCapFilename());
  CHECK(CullingEngineGetLatestCapFilename() == nullptr);
}
