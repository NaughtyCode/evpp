#pragma once

#include <stdint.h>

#include "CullingEngineMacros.h"

enum CullingEngineDebug {
  // P4 stands for Primitive 4. Here Primitive just means Triangle
  kP4Valid0 = 0,
  kP4Valid1 = 1,
  kP4Valid2 = 2,
  kP4Valid3 = 3,
  kP4Valid4 = 4,
  kP4DrawTriangle = 5,

  kFastRowBitCull = 6,
  kFastBlockHizCull = 7,
  kFastBlockEmptyPass = 8,
  kBlockMaskPass = 9,
  kBlockPixelPass = 10,

  kBlockCorrectMinPass = 11,
  kBlockEmptyPass = 12,
  kBlockPixelCull = 13,
  kFastHalfPlaneCull = 14,
  kOccludeeNearClipPass = 15,
  kOccludeeFrustumCull = 16,
  kOccludeeQuery2d = 17,
  kOccludeeCull = 18,
  kOccludeeOnePixelExpandCheck = 19,
  kQueryBlockDoWhileIfSave = 20,
  kInterleaveQuerySkipPass = 21,

  kMaxOccludeeZ = 22,
  kBlockRowCheck = 23,

  kFastBlockDepthCompare = 24,
  kFastPlaneBlockDepthCompareSave = 25,

  kP4Total = 26,
  kP4Rasterized = 27,
  kPrimitiveTotalInput = 28,
  kPrimitiveRasterizedNum = 29,
  kPrimitiveEarlyHizCull = 30,
  kBlockRenderTotal = 31,

  kP4NearClipInput = 32,
  kPrimitiveNearClipeRasterized = 33,

  kOccluderCulled = 34,
  kOccluderRasterized = 35,
  kP4CameraNearPlaneCull = 36,
  kP4BackfaceCull = 37,
  kP4FrustumCull = 38,
  kP4EarlyHizCull = 39,
  kP4EarlyHizCullPass = 40,
  kP4PassCull = 41,
  kP4PassFrustumCull = 42,

  kPrimitiveCameraNearPlaneCull = 43,
  kPrimitiveBackfaceCull = 44,
  kPrimitiveFrustumCull = 45,
  kPrimitiveValidNum = 46,
  kPrimitiveValidNumQuad = 47,
  kPrimitiveDegenerateCull = 48,

  kOccluderFrustumCulled = 49,

  kBlockHizQuickCompare = 53,
  kBlockDoWhileIfSave = 54,
  kBlockTotal = 55,
  kBlockConvexRow10Cull = 56,
  kBlockConvexRow10CullOverhead = 57,
  kBlockOneSureZeroCull = 58,

  kBlockConvexEdge31Cull = 59,
  kBlockConvexEdge31CullRowCheck = 60,
  kBlockConvexEdge31CullRowCheckPass = 80,
  kBlockConvexEdge31CullRowCheckPassExit = 81,
  kBlockConvexAllZeroRow = 82,
  kBlockConvexRow00Cull = 83,
  kBlockConvexRow00CullNextScanRows = 84,
  kBlockConvexEdge24Cull = 85,
  kBlockConvexEdge24Check = 86,

  kBlockPrimitiveMaxLessThanMinCull = 61,
  kBlockMaskJointZeroCull = 62,
  kBlockMaxLessThanMinCull = 63,
  kBlockTotalPrimitives = 64,

  kBlockAabbClipToZero = 65,
  kBlockRenderPartial = 66,
  kBlockRenderFull = 67,
  kBlockRenderInitial = 68,
  kBlockRenderInitialPartial = 69,
  kBlockRenderInitialFull = 70,

  kBlockMinCompute4 = 90,
  kBlockMinUseOne = 91,

  kBlockPacketId = 92,
  kBlockPacketPrimitive = 93,
  kBlockPacketPrimitiveDebug = 94,

  kBlockEmptyBlock = 110,
  kBlockMinValidDepth = 111,

  kRasterizedOccluderTotalTriangles = 115,
  kRasterizedOccluderTotalVertices = 116,

  kOccludeeQueryMaxPass = 120,
  kOccluderQueryMaxPass = 121,
  kCurrentOccludeeIdx = 122,

  kPrimitiveRasterizedQuadNum = 130,
  kBatchQuad4Rasterized = 131,
  kBatchQuadConvex = 132,

  kP4QuadFrustumCull = 140,
  kP4QuadSplit = 141,
  kP4QuadFrustumPass = 142,

  kQuadToTriangleMerge = 150,
  kQuadToTriangleSplit = 151,
  kQuadProcessed = 152,

};

enum DoRasterizerKeyType {
  kDoRasterizerKey0 = 0,
  kDoRasterizerKey1 = 1,
  kDoRasterizerKey2 = 2,
  kDoRasterizerKey3 = 3,
  kDoRasterizerKey5 = 5,
  kDoRasterizerKey7 = 7,
  kDoRasterizerKey8 = 8,
  kDoRasterizerKey9 = 9,
  kDoRasterizerKey10 = 10,
  kDoRasterizerKey11 = 11,
  kDoRasterizerKey13 = 13,
  kDoRasterizerKey15 = 15,

  kDoRasterizerTotal = 64,

  kDoRasterizerCount = 128
};

enum QueryVisibilityErrorType {
  kQueryVisibilityNone = 0,
  kQueryVisibilityInit,
  kQueryVisibilityFrustumCulling,
  kQueryVisibilityNoneQueryOccluderQuery2d,
  kQueryVisibilityQueryOccluderQuery2d,
  kQueryVisibilityOccludee,
  kQueryVisibilityFrustumCullIfClipFrustum
};

struct QueryDebugStates {
 public:
  QueryVisibilityErrorType InvisibleReason;
  QueryVisibilityErrorType VisibleReason;

 public:
  QueryDebugStates()
      : InvisibleReason(kQueryVisibilityNone),
        VisibleReason(kQueryVisibilityNone) {}

 public:
  inline void Reset() {
    InvisibleReason = kQueryVisibilityNone;
    VisibleReason = kQueryVisibilityNone;
  }

  void DumpAndReset(const char* prefix, int usrData);
  const char* GetErrorTypeString(QueryVisibilityErrorType type);
};
