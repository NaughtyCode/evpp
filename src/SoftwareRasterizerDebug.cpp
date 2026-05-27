#include <algorithm>
#include <cassert>
#include <cmath>

#include "CullingEngineAPI.h"
#include "CullingEngineLogger.h"
#include "MathUtility.h"
#include "MemoryUtility.h"
#include "OccluderManager.h"
#include "SoftwareRasterizer.h"

namespace CullingEngine {

void Rasterizer::DumpAll(const float* bBBoxs, bool* results,
                         unsigned int count) {
#if CULLING_ENGINE_ENABLE_RASTERIZER_DEBUG
  {
    DumpBatchQueryResults(bBBoxs, results, count);
    DumpDoRasterizerDebugData();

#if CULLING_ENGINE_ENABLE_DUMP_RASTERIZER_STATES
    DumpRasterizerStates();
#endif  // End of CULLING_ENGINE_ENABLE_DUMP_RASTERIZER_STATES
  }

#endif  // End of CULLING_ENGINE_ENABLE_RASTERIZER_DEBUG
}

void Rasterizer::DumpForInFrustum(__m128& boundsMin, __m128& boundsMax,
                                  __m128& extents) {
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: frustum bounds");

  // boundsMin
  {
    float arr[4];
    _mm_store_ps(arr, boundsMin);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(boundsMin);
  }

  // boundsMax
  {
    float arr[4];
    _mm_store_ps(arr, boundsMax);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(boundsMax);
  }

  // extents
  {
    float arr[4];
    _mm_store_ps(arr, extents);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(extents);
  }

  // extents
  {
    for (int i = 0; i < 6; ++i) {
      float arr[4];
      _mm_store_ps(arr, m_FrustumPlane[i]);
      CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM_EXT(m_FrustumPlane, i);
    }
  }
}

void Rasterizer::DumpRasterizeOccluderInfo(
    CullingEngine::Matrix4x4& viewProj, CullingEngine::OccluderInput* ocInput,
    bool bQueryOccluder) {
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: rasterize occluder info");

  // Matrix
  {
    /*
    CullingEngine::Matrix4x4 LocalToWorldT;
    LocalToWorldT.updateTranspose(ocInput->modelWorld);

    CullingEngine::Matrix4x4 LocalToClipT;
    CullingEngine::Matrix4x4::Multiply(viewProj, LocalToWorldT, LocalToClipT);
    setModelViewProjectionT(LocalToClipT);
    */
  }

  const float* minmaxf = ocInput->inVtx + 2;

  // Origin
  {
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: bounds min=(%f, %f, %f)",
                             minmaxf[0], minmaxf[1], minmaxf[2]);
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: bounds max=(%f, %f, %f)",
                             minmaxf[3], minmaxf[4], minmaxf[5]);
  }

  __m128 extents;
  if (bQueryOccluder) {
    extents = _mm_setr_ps(minmaxf[3], minmaxf[4], minmaxf[5], 0);
  } else {
    extents = _mm_setr_ps(minmaxf[3] - minmaxf[0], minmaxf[4] - minmaxf[1],
                          minmaxf[5] - minmaxf[2], 0);
  }

  __m128 boundsMin = _mm_setr_ps(minmaxf[0], minmaxf[1], minmaxf[2], 1.0f);
  __m128 boundsMax;

  if (bQueryOccluder) {
    boundsMax = _mm_add_ps(boundsMin, extents);
  } else {
    boundsMax = _mm_setr_ps(minmaxf[3], minmaxf[4], minmaxf[5], 1.0f);
  }

  // boundsMin
  {
    float arr[4];
    _mm_store_ps(arr, boundsMin);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(boundsMin);
  }

  // boundsMax
  {
    float arr[4];
    _mm_store_ps(arr, boundsMax);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(boundsMax);
  }

  // extents
  {
    float arr[4];
    _mm_store_ps(arr, extents);
    CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM(extents);
  }

  // extents
  {
    for (int i = 0; i < 6; ++i) {
      float arr[4];
      _mm_store_ps(arr, m_FrustumPlane[i]);
      CULLING_ENGINE_DUMP_VAR_FOR_FRUSTUM_EXT(m_FrustumPlane, i);
    }
  }
}

void Rasterizer::DumpRasterizerStates() {
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: state counters");

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Valid0);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Valid1);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Valid2);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Valid3);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Valid4);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4DrawTriangle);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastRowBitCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastBlockHizCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastBlockEmptyPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMaskPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPixelPass);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockCorrectMinPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockEmptyPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPixelCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastHalfPlaneCull);

  // Occluder
  {
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: occluder counters");

    const char* prefix = "    ";
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccluderCulled);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccluderRasterized);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccluderQueryMaxPass);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccluderFrustumCulled);
  }

  // Occludee
  {
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: occludee counters");

    const char* prefix = "    ";
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeNearClipPass);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeFrustumCull);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeQuery2d);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeCull);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeOnePixelExpandCheck);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kOccludeeQueryMaxPass);
    CULLING_ENGINE_DUMP_DEBUG_ITEM_WITH_PREFIX(kCurrentOccludeeIdx);
  }

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kQueryBlockDoWhileIfSave);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kInterleaveQuerySkipPass);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kMaxOccludeeZ);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRowCheck);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastBlockDepthCompare);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kFastPlaneBlockDepthCompareSave);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Total);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4Rasterized);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveTotalInput);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveRasterizedNum);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveEarlyHizCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderTotal);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4NearClipInput);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveNearClipeRasterized);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4CameraNearPlaneCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4BackfaceCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4FrustumCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4EarlyHizCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4EarlyHizCullPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4PassCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4PassFrustumCull);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveCameraNearPlaneCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveBackfaceCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveFrustumCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveValidNum);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveValidNumQuad);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveDegenerateCull);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockHizQuickCompare);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockDoWhileIfSave);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockTotal);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexRow10Cull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexRow10CullOverhead);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockOneSureZeroCull);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge31Cull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge31CullRowCheck);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge31CullRowCheckPass);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge31CullRowCheckPassExit);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexAllZeroRow);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexRow00Cull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexRow00CullNextScanRows);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge24Cull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockConvexEdge24Check);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPrimitiveMaxLessThanMinCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMaskJointZeroCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMaxLessThanMinCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockTotalPrimitives);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockAabbClipToZero);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderPartial);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderFull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderInitial);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderInitialPartial);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockRenderInitialFull);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMinCompute4);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMinUseOne);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPacketId);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPacketPrimitive);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockPacketPrimitiveDebug);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockEmptyBlock);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBlockMinValidDepth);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kRasterizedOccluderTotalTriangles);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kRasterizedOccluderTotalVertices);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kPrimitiveRasterizedQuadNum);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBatchQuad4Rasterized);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kBatchQuadConvex);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4QuadFrustumCull);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4QuadSplit);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kP4QuadFrustumPass);

  CULLING_ENGINE_DUMP_DEBUG_ITEM(kQuadToTriangleMerge);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kQuadToTriangleSplit);
  CULLING_ENGINE_DUMP_DEBUG_ITEM(kQuadProcessed);

  memset(DebugData, 0, DEBUG_DATA_SIZE * sizeof(uint32_t));
}

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG

void Rasterizer::DumpDoRasterizerDebugData() {
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: rasterize dispatch counters");

  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerTotal);

  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey0);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey1);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey2);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey3);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey5);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey7);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey8);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey9);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey10);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey11);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey13);
  CULLING_ENGINE_DUMP_DO_RASTERIZE_DEBUG(kDoRasterizerKey15);

  ResetDoRasterizerDebugData();
}

#endif

void Rasterizer::DumpBatchQueryResults(const float* bBBoxs, bool* results,
                                       unsigned int count) {
  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: batch query results");

  const char* prefix = "    ";

  // Bounding Box
  {
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: batch query bounds");

    const float* currBBox = bBBoxs;
    for (unsigned int idx = 0; idx < count;
         ++idx, currBBox += CullingEngine::BBOX_STRIDE) {
      CULLING_ENGINE_LOG_DEBUG(
          "Rasterizer debug: bounds[%u] min=(%f, %f, %f) max=(%f, %f, %f)",
          idx, currBBox[0], currBBox[1], currBBox[2], currBBox[3], currBBox[4],
          currBBox[5]);
    }
  }

  // Results
  {
    CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: batch query visibility");
    for (unsigned int idx = 0; idx < count; ++idx) {
      CULLING_ENGINE_DUMP_BATCH_QUERY_WITH_RESULTS(idx);
    }
  }
}

}  // namespace CullingEngine
