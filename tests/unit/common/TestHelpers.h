#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "CullingEngineAPI.h"

namespace CullingEngineTests {
class EngineHandle {
 public:
  EngineHandle(unsigned int width = 64, unsigned int height = 8,
               float nearPlane = 1.0f)
      : mHandle(CullingEngineInit(width, height, nearPlane)) {}

  ~EngineHandle() {
    if (mHandle != nullptr) {
      CullingEngineSet(mHandle, CULLING_ENGINE_DESTROY, 1);
      mHandle = nullptr;
    }
  }

  EngineHandle(const EngineHandle&) = delete;
  EngineHandle& operator=(const EngineHandle&) = delete;

  void* Get() const { return mHandle; }

  explicit operator bool() const { return mHandle != nullptr; }

 private:
  void* mHandle = nullptr;
};

inline std::array<float, 16> IdentityMatrix() {
  return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
}

inline std::array<float, 3> CameraPosition() { return {0.0f, 0.0f, 0.0f}; }

inline std::array<float, 3> CameraDirection() { return {0.0f, 0.0f, 1.0f}; }

inline std::array<float, 6> VisibleBox(float minZ = 1.0f, float maxZ = 2.0f) {
  return {-0.25f, -0.25f, minZ, 0.25f, 0.25f, maxZ};
}

inline bool StartDefaultFrame(void* engine) {
  auto cameraPosition = CameraPosition();
  auto cameraDirection = CameraDirection();
  auto viewProjection = IdentityMatrix();
  return CullingEngineStartNewFrame(engine, cameraPosition.data(),
                                    cameraDirection.data(),
                                    viewProjection.data(), false);
}

inline std::filesystem::path MakeTempDirectory(const std::string& name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}
}  // namespace CullingEngineTests
