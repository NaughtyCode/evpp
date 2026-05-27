#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "CullingEngineMacros.h"

namespace CullingEngineExample {
struct Mesh {
  std::string name;
  std::vector<float> vertices;
  std::vector<unsigned short> indices;
};

struct RenderSettings {
  unsigned int width = 1024;
  unsigned int height = 1024;
  float nearPlane = CULLING_ENGINE_MIN_NEAR_PLANE;
  unsigned int renderMode = CULLING_ENGINE_RENDER_MODE_FULL;
  bool counterClockwise = true;
  bool visualizeDepth = true;
};

struct Camera {
  std::array<float, 3> position{0.0f, 0.0f, 0.0f};
  std::array<float, 3> direction{0.0f, 0.0f, 1.0f};
  std::array<float, 16> viewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                       0.0f, 0.0f, 0.0f, 1.0f};
  bool rowMajor = false;
};

struct MeshAsset {
  std::string id;
  std::filesystem::path file;
  bool normalize = true;
};

struct Instance {
  const Mesh* mesh = nullptr;
  std::string name;
  std::string meshId;
  std::array<float, 16> localToWorld{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                     0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f};
  bool rowMajor = false;
  bool backfaceCull = false;
};

struct Scene {
  RenderSettings render;
  Camera camera;
  std::filesystem::path assetRoot;
  std::optional<std::filesystem::path> outputPath;
  std::vector<MeshAsset> meshes;
  std::vector<Instance> instances;
};

std::filesystem::path DefaultAssetsDir();
std::filesystem::path DefaultScenePath(const std::filesystem::path& assetsDir);
std::filesystem::path DefaultOutputPath();

Scene CreateDefaultDepthScene(const std::filesystem::path& assetsDir);
Scene LoadSceneFromJson(const std::filesystem::path& scenePath,
                        const std::filesystem::path& fallbackAssetsDir);

void WriteDefaultDepthSceneJson(const std::filesystem::path& scenePath,
                                const std::filesystem::path& assetsDir,
                                const std::filesystem::path& outputPath);

void LoadSceneMeshes(Scene& scene, std::vector<Mesh>& meshes);
void ValidateRenderSettings(const RenderSettings& render);
}  // namespace CullingEngineExample
