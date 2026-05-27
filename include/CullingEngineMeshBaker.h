#pragma once

#include <memory>

#include "OccluderManager.h"
#include "OccluderQuadDecomposition.h"
#include "PlatformSIMD.h"

struct CULLING_ENGINE_API AutoMeshBaker {
 public:
  AutoMeshBaker(int* outputCompressSize, const float* vertices,
                const unsigned short* indices, unsigned int nVert,
                unsigned int nIdx, float quadAngle, bool enableBackfaceCull,
                bool counterClockWise, int SquareTerrainAxisPoints,
                bool bDumpBakeInfo = true);

  ~AutoMeshBaker();

 public:
  inline unsigned short* GetBakeOutputBuffer() { return m_pBakeOutputBuffer; }

 public:
  void* m_pOccluderBakeBuffer;
  unsigned short* m_pBakeOutputBuffer;
};
