#include "CullingEngineMeshBaker.h"

#include <chrono>

#include "CullingEngineAPI.h"
#include "CullingEngineCore.h"
#include "CullingEngineLogger.h"
#include "MathUtility.h"
#include "OccluderManager.h"
#include "OccluderQuadDecomposition.h"
#include "SoftwareRasterizer.h"

namespace {
bool HasValidIndexRange(const unsigned short* indices, unsigned int nVert,
                        unsigned int nIdx) {
  if (indices == nullptr || nVert == 0 || nIdx == 0 || (nIdx % 3) != 0) {
    return false;
  }

  for (unsigned int idx = 0; idx < nIdx; ++idx) {
    if (indices[idx] >= nVert) {
      return false;
    }
  }

  return true;
}
}  // namespace

#ifdef CULLING_ENGINE_NATIVE

unsigned short* CullingEngineMeshBake(
    void* occluderBakeBuffer, int* outputCompressSize, const float* vertices,
    const unsigned short* indices, unsigned int nVert, unsigned int nIdx,
    float quadAngle, bool enableBackfaceCull, bool counterClockWise,
    int squareTerrainAxisPoints) {
  try {
    if (!outputCompressSize) {
      CULLING_ENGINE_LOG_WARNING(
          "Mesh bake failed: outputCompressSize is null");
      return nullptr;
    }

    CullingEngine::OccluderBakeBuffer* pOccluderBakeBuffer =
        (CullingEngine::OccluderBakeBuffer*)occluderBakeBuffer;
    if (!pOccluderBakeBuffer) {
      CULLING_ENGINE_LOG_WARNING("Mesh bake failed: bake buffer is null");
      *outputCompressSize = 0;
      return nullptr;
    }
    if (vertices == nullptr || !HasValidIndexRange(indices, nVert, nIdx)) {
      CULLING_ENGINE_LOG_WARNING("Mesh bake failed: input mesh is invalid");
      *outputCompressSize = 0;
      return nullptr;
    }

    unsigned short* data = CullingEngine::OccluderQuad::CullingEngineMeshBake(
        pOccluderBakeBuffer, outputCompressSize, vertices, indices, nVert, nIdx,
        quadAngle, enableBackfaceCull, counterClockWise,
        squareTerrainAxisPoints);

    return data;
  } catch (...) {
    if (outputCompressSize != nullptr) {
      *outputCompressSize = 0;
    }
    try {
      CULLING_ENGINE_LOG_ERROR(
          "CullingEngineMeshBake failed due to an unhandled C++ exception");
    } catch (...) {
    }
    return nullptr;
  }
}

AutoMeshBaker::AutoMeshBaker(int* outputCompressSize, const float* vertices,
                             const unsigned short* indices, unsigned int nVert,
                             unsigned int nIdx, float quadAngle,
                             bool enableBackfaceCull, bool counterClockWise,
                             int SquareTerrainAxisPoints, bool bDumpBakeInfo)
    : m_pOccluderBakeBuffer(nullptr), m_pBakeOutputBuffer(nullptr) {
  auto startTime = std::chrono::high_resolution_clock::now();

  m_pOccluderBakeBuffer = CullingEngineCreateOccluderBakeBuffer();
  m_pBakeOutputBuffer = CullingEngineMeshBake(
      m_pOccluderBakeBuffer, outputCompressSize, vertices, indices, nVert, nIdx,
      quadAngle, enableBackfaceCull, counterClockWise, SquareTerrainAxisPoints);

  if (m_pBakeOutputBuffer != nullptr && bDumpBakeInfo) {
    auto endTime = std::chrono::high_resolution_clock::now();
    size_t totalTimeInMS =
        (size_t)std::chrono::duration_cast<std::chrono::microseconds>(endTime -
                                                                      startTime)
            .count();

    size_t rawSize =
        static_cast<size_t>(nVert) * 12u + static_cast<size_t>(nIdx) * 2u;
    size_t compressSize = static_cast<size_t>(*outputCompressSize) * 2u;
    float compressSizeRatio =
        rawSize > 0
            ? (static_cast<float>(compressSize) / static_cast<float>(rawSize)) *
                  100.0f
            : 0.0f;

    CULLING_ENGINE_LOG_INFO(
        "Auto mesh bake result: vertices=%u compressedBytes=%zu rawBytes=%zu "
        "ratio=%.2f%%",
        nVert, compressSize, rawSize, compressSizeRatio);

    CULLING_ENGINE_LOG_INFO(
        "Auto mesh bake time: %.3f ms, faces=%u, vertices=%u",
        (totalTimeInMS * 1.0 / 1000), nIdx / 3, nVert);
  }
}

AutoMeshBaker::~AutoMeshBaker() {
  if (m_pOccluderBakeBuffer) {
    CullingEngineDestroyOccluderBakeBuffer(m_pOccluderBakeBuffer);
    m_pOccluderBakeBuffer = nullptr;
  }
}

void* CullingEngineCreateOccluderBakeBuffer() {
  try {
    CullingEngine::OccluderBakeBuffer* pOccluderBakeBuffer =
        new CullingEngine::OccluderBakeBuffer();
    return (void*)pOccluderBakeBuffer;
  } catch (...) {
    try {
      CULLING_ENGINE_LOG_ERROR(
          "CullingEngineCreateOccluderBakeBuffer failed due to an unhandled "
          "C++ exception");
    } catch (...) {
    }
    return nullptr;
  }
}

void CullingEngineDestroyOccluderBakeBuffer(void* occluderBakeBuffer) {
  CullingEngine::OccluderBakeBuffer* pOccluderBakeBuffer =
      (CullingEngine::OccluderBakeBuffer*)occluderBakeBuffer;
  if (!pOccluderBakeBuffer) {
    CULLING_ENGINE_LOG_WARNING(
        "Destroy occluder bake buffer ignored: buffer is null");
    return;
  }

  delete pOccluderBakeBuffer;
}

#else  // Else of CULLING_ENGINE_NATIVE

unsigned short* CullingEngineMeshBake(
    void* occluderBakeBuffer, int* outputCompressSize, const float* vertices,
    const unsigned short* indices, unsigned int nVert, unsigned int nIdx,
    float quadAngle, bool enableBackfaceCull, bool counterClockWise,
    int TerrainGridAxisPoint) {
  (void)occluderBakeBuffer;
  if (outputCompressSize != nullptr) {
    *outputCompressSize = 0;
  }
  (void)vertices;
  (void)indices;
  (void)nVert;
  (void)nIdx;
  (void)quadAngle;
  (void)enableBackfaceCull;
  (void)counterClockWise;
  (void)TerrainGridAxisPoint;
  return nullptr;
}

unsigned short* CullingEngineAutoMeshBakeInner(
    int* outputCompressSize, const float* vertices,
    const unsigned short* indices, unsigned int nVert, unsigned int nIdx,
    float quadAngle, bool enableBackfaceCull, bool counterClockWise,
    int SquareTerrainAxisPoints, bool bDumpBakeInfo) {
  return nullptr;
}

AutoMeshBaker::AutoMeshBaker(int* outputCompressSize, const float* vertices,
                             const unsigned short* indices, unsigned int nVert,
                             unsigned int nIdx, float quadAngle,
                             bool enableBackfaceCull, bool counterClockWise,
                             int SquareTerrainAxisPoints, bool bDumpBakeInfo)
    : m_pOccluderBakeBuffer(nullptr), m_pBakeOutputBuffer(nullptr) {}

AutoMeshBaker::~AutoMeshBaker() {}

void* CullingEngineCreateOccluderBakeBuffer() { return nullptr; }

void CullingEngineDestroyOccluderBakeBuffer(void* pOccluderBakeBuffer) {}

#endif  // End of CULLING_ENGINE_NATIVE
