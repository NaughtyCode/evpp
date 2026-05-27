#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include "CullingEngineImageDump.h"
#include "CullingEngineLogger.h"
#include "CullingEngineMacros.h"

#if defined(CULLING_ENGINE_NATIVE_DEBUG) && defined(CULLING_ENGINE_NATIVE)

namespace {
uint64_t ReadMetadataU64(const unsigned char* metadata, std::size_t index) {
  uint64_t value = 0;
  std::memcpy(&value, metadata + index * sizeof(value), sizeof(value));
  return value;
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
  constexpr uint32_t kMod = 65521u;
  uint32_t a = 1u;
  uint32_t b = 0u;
  for (unsigned char value : data) {
    a = (a + value) % kMod;
    b = (b + a) % kMod;
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
  zlib.reserve(data.size() + 16u + (data.size() / 65535u) * 5u);
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

bool WritePng(const std::string& filename, const unsigned char* inputBuffer,
              unsigned int width, unsigned int height, unsigned int channels,
              bool flipVertical) {
  if (filename.empty() || inputBuffer == nullptr || width == 0 || height == 0 ||
      (channels != 1u && channels != 3u)) {
    return false;
  }

  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  const std::size_t rowBytes = static_cast<std::size_t>(width) * channels;
  if (rowBytes / channels != width || rowBytes == kMaxSize ||
      height > (kMaxSize / (rowBytes + 1u))) {
    return false;
  }

  std::vector<unsigned char> rawRows;
  rawRows.reserve((rowBytes + 1u) * height);
  for (unsigned int y = 0; y < height; ++y) {
    rawRows.push_back(0);
    const unsigned int sourceY = flipVertical ? (height - 1u - y) : y;
    const unsigned char* rowBegin =
        inputBuffer + static_cast<std::size_t>(sourceY) * rowBytes;
    rawRows.insert(rawRows.end(), rowBegin, rowBegin + rowBytes);
  }

  std::vector<unsigned char> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

  std::vector<unsigned char> ihdr;
  WriteBigEndian32(ihdr, width);
  WriteBigEndian32(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(channels == 1u ? 0 : 2);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  AppendChunk(png, "IHDR", ihdr);

  std::vector<unsigned char> idat = ZlibStore(rawRows);
  AppendChunk(png, "IDAT", idat);

  const std::vector<unsigned char> iend;
  AppendChunk(png, "IEND", iend);

  const std::filesystem::path outputPath(filename);
  const std::filesystem::path parent = outputPath.parent_path();
  if (!parent.empty()) {
    std::error_code errorCode;
    std::filesystem::create_directories(parent, errorCode);
    if (errorCode) {
      CULLING_ENGINE_LOG_WARNING("Failed to create image directory: %s",
                          parent.string().c_str());
      return false;
    }
  }

  std::ofstream output(outputPath, std::ios_base::out | std::ios_base::binary);
  if (!output) {
    CULLING_ENGINE_LOG_WARNING("Failed to open image for writing: %s",
                               filename.c_str());
    return false;
  }

  output.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
  if (!output) {
    CULLING_ENGINE_LOG_WARNING("Failed to write image: %s", filename.c_str());
    return false;
  }
  return true;
}
}  // namespace

bool DumpOccluderOccludeeColorImage(const std::string& filename,
                                    unsigned char* inputBuffer,
                                    unsigned int width, unsigned int height) {
  if (filename.empty() || inputBuffer == nullptr || width == 0 || height == 0) {
    return false;
  }

  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  if (height != 0 && static_cast<std::size_t>(width) > kMaxSize / height) {
    return false;
  }

  const size_t resolution = static_cast<size_t>(width) * height;
  if (resolution < sizeof(uint64_t) || resolution > kMaxSize / 3u) {
    return false;
  }

  std::vector<uint8_t> data(resolution * 2, 0);
  memcpy(data.data(), inputBuffer, resolution);

  const unsigned char* metaData = inputBuffer + resolution;
  uint64_t totalSize = ReadMetadataU64(metaData, 0);
  const uint64_t metadataCapacity =
      static_cast<uint64_t>(resolution / sizeof(uint64_t));
  if (totalSize >= metadataCapacity && totalSize != 0) {
    CULLING_ENGINE_LOG_WARNING(
        "Invalid occludee metadata count: %llu for image resolution %zu",
        static_cast<unsigned long long>(totalSize), resolution);
    return false;
  }
  if (totalSize > 0) {
    for (uint64_t idx = 0; idx + 1 < totalSize; idx += 2) {
      uint64_t pos =
          ReadMetadataU64(metaData, static_cast<std::size_t>(idx + 1));
      uint64_t stateDepth =
          ReadMetadataU64(metaData, static_cast<std::size_t>(idx + 2));

      int minX = static_cast<int>(pos >> 48);
      int maxX = static_cast<int>((pos >> 32) & 0xFFFF);
      int minY = static_cast<int>((pos >> 16) & 0xFFFF);
      int maxY = static_cast<int>(pos & 0xFFFF);
      int visible = static_cast<int>(stateDepth & 1);
      uint8_t depth = static_cast<uint8_t>(stateDepth >> 48);

      if (minX > maxX || minY > maxY || maxX >= static_cast<int>(width) ||
          maxY >= static_cast<int>(height)) {
        CULLING_ENGINE_LOG_WARNING(
            "Skip invalid occludee metadata: min(%d,%d) max(%d,%d) "
            "image(%u,%u)",
            minX, minY, maxX, maxY, width, height);
        continue;
      }

      uint8_t* dest = data.data() + width * minY;
      for (int k = minX; k <= maxX; k++) {
        dest[k] = depth;
        dest[k + resolution] = 1 + visible;
      }
      dest = data.data() + width * maxY;
      for (int k = minX; k <= maxX; k++) {
        dest[k] = depth;
        dest[k + resolution] = 1 + visible;
      }

      for (int k = minY + 1; k <= maxY - 1; k++) {
        dest = data.data() + width * k;

        dest[minX] = depth;
        dest[minX + resolution] = 1 + visible;
        dest[maxX] = depth;
        dest[maxX + resolution] = 1 + visible;
      }
    }
  }

  uint8_t mask[12] = {};
  mask[0] = mask[1] = mask[2] = 0xFF;
  mask[4] = 0xFF;
  mask[8] = 0;
  mask[9] = 0xFF;

  std::vector<unsigned char> rgb(resolution * 3u, 0);
  for (unsigned int ny = 0; ny < height; ny++) {
    for (unsigned int nx = 0; nx < width; nx++) {
      size_t sourceIdx = nx + static_cast<size_t>(height - 1 - ny) * width;
      uint8_t v = data[sourceIdx];
      uint8_t maskIdx = data[sourceIdx + resolution];
      const uint8_t* pMask = mask + (maskIdx << 2);
      const size_t destIdx =
          (static_cast<size_t>(ny) * width + nx) * static_cast<size_t>(3);
      rgb[destIdx] = v & pMask[0];
      rgb[destIdx + 1] = v & pMask[1];
      rgb[destIdx + 2] = v & pMask[2];
    }
  }

  return WritePng(filename, rgb.data(), width, height, 3, false);
}

void DumpGrayImage(const std::string& filename,
                   const unsigned char* inputBuffer, unsigned int width,
                   unsigned int height) {
  if (filename.empty() || inputBuffer == nullptr || width == 0 || height == 0) {
    return;
  }

  if (WritePng(filename, inputBuffer, width, height, 1, true)) {
    CULLING_ENGINE_LOG_INFO("Saved grayscale PNG: %s", filename.c_str());
  }
}

void DumpImageMOC(const std::string& filename, const unsigned char* inputBuffer,
                  unsigned int width, unsigned int height) {
  if (filename.empty() || inputBuffer == nullptr || width == 0 || height == 0) {
    return;
  }

  if (WritePng(filename, inputBuffer, width, height, 1, false)) {
    CULLING_ENGINE_LOG_INFO("Saved MOC PNG: %s", filename.c_str());
  }
}

#else

bool DumpOccluderOccludeeColorImage(const std::string& filename,
                                    unsigned char* inputBuffer,
                                    unsigned int width, unsigned int height) {
  (void)filename;
  (void)inputBuffer;
  (void)width;
  (void)height;
  return false;
}

void DumpGrayImage(const std::string& filename,
                   const unsigned char* inputBuffer, unsigned int width,
                   unsigned int height) {
  (void)filename;
  (void)inputBuffer;
  (void)width;
  (void)height;
}

void DumpImageMOC(const std::string& filename, const unsigned char* inputBuffer,
                  unsigned int width, unsigned int height) {
  (void)filename;
  (void)inputBuffer;
  (void)width;
  (void)height;
}

#endif
