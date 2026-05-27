#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "CullingEngineAPI.h"
#include "scene_assets.h"

namespace {
using CullingEngineExample::DefaultAssetsDir;
using CullingEngineExample::DefaultOutputPath;
using CullingEngineExample::DefaultScenePath;
using CullingEngineExample::LoadSceneFromJson;
using CullingEngineExample::LoadSceneMeshes;
using CullingEngineExample::Mesh;
using CullingEngineExample::Scene;
using CullingEngineExample::ValidateRenderSettings;
using CullingEngineExample::WriteDefaultDepthSceneJson;

struct Options {
  std::filesystem::path assetsDir = DefaultAssetsDir();
  std::optional<std::filesystem::path> scenePath;
  std::optional<std::filesystem::path> generateScenePath;
  std::filesystem::path outputPath = DefaultOutputPath();
  unsigned int width = 1024;
  unsigned int height = 1024;
  bool hasOutputOverride = false;
  bool widthOverride = false;
  bool heightOverride = false;
  bool noRender = false;
};

std::filesystem::path ResolveSceneArgument(
    const std::filesystem::path& assetsDir,
    const std::optional<std::filesystem::path>& value) {
  if (!value.has_value()) {
    return DefaultScenePath(assetsDir);
  }

  const std::filesystem::path& path = *value;
  if (path.is_absolute()) {
    return path;
  }

  return assetsDir / "scenes" / path;
}

void WriteBigEndian32(std::vector<unsigned char>& output, uint32_t value) {
  output.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
  output.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
  output.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
  output.push_back(static_cast<unsigned char>(value & 0xFFu));
}

uint32_t Crc32(const unsigned char* data, std::size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const std::vector<unsigned char>& data) {
  constexpr uint32_t mod = 65521u;
  uint32_t a = 1u;
  uint32_t b = 0u;
  for (unsigned char value : data) {
    a = (a + value) % mod;
    b = (b + a) % mod;
  }
  return (b << 16) | a;
}

void AppendChunk(std::vector<unsigned char>& png, const char type[4],
                 const std::vector<unsigned char>& data) {
  WriteBigEndian32(png, static_cast<uint32_t>(data.size()));

  const std::size_t crcStart = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), data.begin(), data.end());

  const uint32_t crc = Crc32(png.data() + crcStart, png.size() - crcStart);
  WriteBigEndian32(png, crc);
}

std::vector<unsigned char> ZlibStore(const std::vector<unsigned char>& data) {
  std::vector<unsigned char> zlib;
  zlib.reserve(data.size() + 16 + (data.size() / 65535u) * 5u);
  zlib.push_back(0x78);
  zlib.push_back(0x01);

  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::size_t remaining = data.size() - offset;
    const uint16_t blockSize =
        static_cast<uint16_t>(std::min<std::size_t>(remaining, 65535u));
    const bool finalBlock = (offset + blockSize) == data.size();
    zlib.push_back(finalBlock ? 0x01 : 0x00);
    zlib.push_back(static_cast<unsigned char>(blockSize & 0xFFu));
    zlib.push_back(static_cast<unsigned char>((blockSize >> 8) & 0xFFu));
    const uint16_t inverseSize = static_cast<uint16_t>(~blockSize);
    zlib.push_back(static_cast<unsigned char>(inverseSize & 0xFFu));
    zlib.push_back(static_cast<unsigned char>((inverseSize >> 8) & 0xFFu));
    zlib.insert(zlib.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                data.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
    offset += blockSize;
  }

  WriteBigEndian32(zlib, Adler32(data));
  return zlib;
}

void WriteGrayPng(const std::filesystem::path& path,
                  const std::vector<unsigned char>& pixels, unsigned int width,
                  unsigned int height) {
  if (pixels.size() < static_cast<std::size_t>(width) * height) {
    throw std::runtime_error("Not enough pixels for PNG output");
  }

  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::vector<unsigned char> rawRows;
  rawRows.reserve((static_cast<std::size_t>(width) + 1u) * height);
  for (unsigned int y = 0; y < height; ++y) {
    rawRows.push_back(0);
    const unsigned int sourceY = height - 1u - y;
    const auto rowBegin =
        pixels.begin() +
        static_cast<std::ptrdiff_t>(static_cast<std::size_t>(sourceY) * width);
    rawRows.insert(rawRows.end(), rowBegin, rowBegin + width);
  }

  std::vector<unsigned char> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

  std::vector<unsigned char> ihdr;
  WriteBigEndian32(ihdr, width);
  WriteBigEndian32(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  AppendChunk(png, "IHDR", ihdr);

  std::vector<unsigned char> idat = ZlibStore(rawRows);
  AppendChunk(png, "IDAT", idat);

  const std::vector<unsigned char> iend;
  AppendChunk(png, "IEND", iend);

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open PNG for writing: " +
                             path.string());
  }
  output.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
  if (!output) {
    throw std::runtime_error("Failed while writing PNG: " + path.string());
  }
}

std::vector<unsigned char> BuildDepthVisualization(
    const std::vector<unsigned char>& depth) {
  std::array<std::size_t, 256> histogram{};
  for (unsigned char value : depth) {
    ++histogram[value];
  }

  const std::size_t nonzeroCount = depth.size() - histogram[0];
  if (nonzeroCount == 0) {
    return depth;
  }

  const std::size_t tailCutoff = std::max<std::size_t>(1, nonzeroCount / 100);

  int low = 1;
  std::size_t cumulative = 0;
  for (; low < 256; ++low) {
    cumulative += histogram[static_cast<std::size_t>(low)];
    if (cumulative >= tailCutoff) {
      break;
    }
  }

  int high = 255;
  cumulative = 0;
  for (; high > 0; --high) {
    cumulative += histogram[static_cast<std::size_t>(high)];
    if (cumulative >= tailCutoff) {
      break;
    }
  }

  if (low >= high) {
    return depth;
  }

  std::vector<unsigned char> visualized(depth.size(), 0);
  for (std::size_t i = 0; i < depth.size(); ++i) {
    const int value = depth[i];
    if (value == 0) {
      continue;
    }
    const int clamped = std::clamp(value, low, high);
    const float normalized =
        static_cast<float>(clamped - low) / static_cast<float>(high - low);
    visualized[i] =
        static_cast<unsigned char>(32 + std::lround(normalized * 223.0f));
  }
  return visualized;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto requireValue = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + name);
      }
      return argv[++i];
    };

    if (arg == "--assets-dir") {
      options.assetsDir = requireValue("--assets-dir");
    } else if (arg == "--scene") {
      options.scenePath = requireValue("--scene");
    } else if (arg == "--generate-scene") {
      options.generateScenePath = requireValue("--generate-scene");
    } else if (arg == "--output") {
      options.outputPath = requireValue("--output");
      options.hasOutputOverride = true;
    } else if (arg == "--width") {
      options.width =
          static_cast<unsigned int>(std::stoul(requireValue("--width")));
      options.widthOverride = true;
    } else if (arg == "--height") {
      options.height =
          static_cast<unsigned int>(std::stoul(requireValue("--height")));
      options.heightOverride = true;
    } else if (arg == "--no-render") {
      options.noRender = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: CullingEngine_depth_scene_demo [--assets-dir PATH]\n"
          << "                                      [--scene NAME_OR_PATH]\n"
          << "                                      [--generate-scene "
             "NAME_OR_PATH] [--no-render]\n"
          << "                                      [--output PATH] [--width "
             "N] [--height N]\n\n"
          << "Relative scene paths are resolved under <assets-dir>/scenes.\n"
          << "Model assets are loaded from <assetRoot>/models as declared by "
             "the scene JSON.\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  return options;
}

void ValidateOptions(const Options& options) {
  if (options.width < 64 || (options.width % 64u) != 0u) {
    throw std::runtime_error("Width must be at least 64 and divisible by 64");
  }
  if (options.height < 8 || (options.height % 8u) != 0u) {
    throw std::runtime_error("Height must be at least 8 and divisible by 8");
  }
  if (options.noRender && !options.generateScenePath.has_value()) {
    throw std::runtime_error("--no-render requires --generate-scene");
  }
}

void ApplyOptionOverrides(Scene& scene, const Options& options) {
  if (options.widthOverride) {
    scene.render.width = options.width;
  }
  if (options.heightOverride) {
    scene.render.height = options.height;
  }
  if (options.hasOutputOverride) {
    scene.outputPath = options.outputPath;
  }
  if (!scene.outputPath.has_value()) {
    scene.outputPath = options.outputPath;
  }
}

Scene LoadSceneForOptions(const Options& options) {
  if (options.generateScenePath.has_value()) {
    const std::filesystem::path generatePath =
        ResolveSceneArgument(options.assetsDir, options.generateScenePath);
    WriteDefaultDepthSceneJson(generatePath, options.assetsDir,
                               options.outputPath);
    std::cout << "Generated scene JSON: "
              << std::filesystem::absolute(generatePath).string() << '\n';
  }

  if (options.noRender) {
    return Scene{};
  }

  std::filesystem::path scenePath =
      ResolveSceneArgument(options.assetsDir, options.scenePath);
  if (options.generateScenePath.has_value() && !options.scenePath.has_value()) {
    scenePath =
        ResolveSceneArgument(options.assetsDir, options.generateScenePath);
  }

  Scene scene;
  if (std::filesystem::exists(scenePath)) {
    scene = LoadSceneFromJson(scenePath, options.assetsDir);
    std::cout << "Loaded scene JSON: "
              << std::filesystem::absolute(scenePath).string() << '\n';
  } else if (!options.scenePath.has_value()) {
    scene = CullingEngineExample::CreateDefaultDepthScene(options.assetsDir);
    std::cout << "Using built-in default scene because no scene JSON exists at "
              << std::filesystem::absolute(scenePath).string() << '\n';
  } else {
    throw std::runtime_error("Scene JSON does not exist: " +
                             scenePath.string());
  }

  ApplyOptionOverrides(scene, options);
  ValidateRenderSettings(scene.render);
  return scene;
}

void DestroyEngine(void* engine) {
  if (engine != nullptr) {
    CullingEngineSet(engine, CULLING_ENGINE_DESTROY, 1);
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    ValidateOptions(options);

    Scene scene = LoadSceneForOptions(options);
    if (options.noRender) {
      return 0;
    }

    std::vector<Mesh> meshes;
    LoadSceneMeshes(scene, meshes);
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
      std::cout << "Loaded " << scene.meshes[i].id << " from "
                << (scene.assetRoot / "models" / scene.meshes[i].file)
                       .lexically_normal()
                       .string()
                << " vertices=" << (meshes[i].vertices.size() / 3)
                << " triangles=" << (meshes[i].indices.size() / 3) << '\n';
    }
    std::cout << "Scene meshes=" << meshes.size()
              << " instances=" << scene.instances.size() << '\n';

    void* engine = CullingEngineInit(scene.render.width, scene.render.height,
                                     scene.render.nearPlane);
    if (engine == nullptr) {
      throw std::runtime_error("CullingEngineInit failed");
    }

    const std::unique_ptr<void, decltype(&DestroyEngine)> engineGuard(
        engine, DestroyEngine);

    CullingEngineSet(engine, CULLING_ENGINE_RENDER_MODE,
                     scene.render.renderMode);
    CullingEngineSet(engine, CULLING_ENGINE_SET_CCW,
                     scene.render.counterClockwise ? 1u : 0u);

    if (!CullingEngineStartNewFrame(
            engine, scene.camera.position.data(), scene.camera.direction.data(),
            scene.camera.viewProjection.data(), scene.camera.rowMajor)) {
      throw std::runtime_error("CullingEngineStartNewFrame failed");
    }

    for (const CullingEngineExample::Instance& instance : scene.instances) {
      CullingEngineRenderOccluder(
          engine, instance.mesh->vertices.data(), instance.mesh->indices.data(),
          static_cast<unsigned int>(instance.mesh->vertices.size() / 3),
          static_cast<unsigned int>(instance.mesh->indices.size()),
          instance.localToWorld.data(), instance.rowMajor,
          instance.backfaceCull);
    }

    if (!CullingEngineSet(engine, CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER, 0)) {
      throw std::runtime_error("Failed to flush submitted occluders");
    }

    int widthHeight[2] = {};
    if (!CullingEngineSync(engine, CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT,
                           widthHeight)) {
      throw std::runtime_error("Failed to query depth buffer dimensions");
    }

    const std::size_t pixelCount = static_cast<std::size_t>(widthHeight[0]) *
                                   static_cast<std::size_t>(widthHeight[1]);
    std::vector<unsigned char> depth(pixelCount + sizeof(uint64_t), 0);
    if (!CullingEngineSync(engine, CULLING_ENGINE_GET_DEPTH_MAP,
                           depth.data())) {
      throw std::runtime_error("Failed to read depth map");
    }
    depth.resize(pixelCount);

    auto [minIt, maxIt] = std::minmax_element(depth.begin(), depth.end());
    const std::size_t coveredPixels = static_cast<std::size_t>(
        std::count_if(depth.begin(), depth.end(),
                      [](unsigned char value) { return value != 0; }));

    if (coveredPixels == 0 || minIt == depth.end() || *minIt == *maxIt) {
      throw std::runtime_error("Rendered depth map is blank or constant");
    }

    std::vector<unsigned char> depthPng =
        scene.render.visualizeDepth ? BuildDepthVisualization(depth) : depth;
    auto [pngMinIt, pngMaxIt] =
        std::minmax_element(depthPng.begin(), depthPng.end());

    const std::filesystem::path outputPath =
        scene.outputPath.value_or(options.outputPath);
    WriteGrayPng(outputPath, depthPng,
                 static_cast<unsigned int>(widthHeight[0]),
                 static_cast<unsigned int>(widthHeight[1]));

    std::cout << "Saved depth PNG: "
              << std::filesystem::absolute(outputPath).string() << '\n'
              << "Raw depth range: " << static_cast<int>(*minIt) << ".."
              << static_cast<int>(*maxIt) << '\n'
              << "PNG depth range: " << static_cast<int>(*pngMinIt) << ".."
              << static_cast<int>(*pngMaxIt) << '\n'
              << "Covered pixels: " << coveredPixels << "/" << pixelCount
              << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "depth_scene_demo failed: " << ex.what() << '\n';
    return 1;
  }
}
