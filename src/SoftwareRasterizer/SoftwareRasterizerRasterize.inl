/*
 * Chooses the specialized rasterization template for a submitted occluder mesh.
 * The dispatch key encodes clipping, compressed primitive data, super-compress
 * layout, and back-face culling so the inner loop stays compile-time constant.
 */
void Rasterizer::DoRasterize(CullingEngine::OccluderMesh& raw) {
  int key = (int)this->mOccluderCache.NeedsClipping << 1;  // 2
  key |= (int)(raw.Indices == nullptr);                    // 1
  key |= raw.EnableBackface << 3;                          // 8
  key |= raw.SuperCompress
         << 2;  // raw.Indices must be false  //4,   remove case of 4 6 12 14

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG

  int realKey = -1;

#endif

  switch (key) {
    case kDoRasterizerKey0:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey0);
    case kDoRasterizerKey1:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey1);
    case kDoRasterizerKey2:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey2);
    case kDoRasterizerKey3:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey3);
    case kDoRasterizerKey5:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey5);
    case kDoRasterizerKey7:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey7);
    case kDoRasterizerKey8:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey8);
    case kDoRasterizerKey9:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey9);
    case kDoRasterizerKey10:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey10);
    case kDoRasterizerKey11:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey11);
    case kDoRasterizerKey13:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey13);
    case kDoRasterizerKey15:
      CULLING_ENGINE_MAKE_DO_RASTERIZE_SWITCH_CODE(kDoRasterizerKey15);
    default:
      break;
  }

#if CULLING_ENGINE_ENABLE_DO_RASTERIZE_DEBUG
  if (realKey > 0) {
    ++m_DoRasterizerDebugData[kDoRasterizerTotal];
    ++m_DoRasterizerDebugData[realKey];
  }
#endif
}

/*
 * Transposes four triangle vertex pointers into the structure-of-arrays layout
 * consumed by SIMD projection. It packs X, Y, and Z values for four primitives
 * into contiguous lanes.
 */
inline static void Transpose(const float** triangleData, float* dataf) {
  //_MM_TRANSPOSE4_PS_43(dataArray[0], dataArray[3], dataArray[6],
  // dataArray[9]);

  // 0 12 24
  const float* f = triangleData[0];
  dataf[0] = f[0];
  dataf[12] = f[1];
  dataf[24] = f[2];

  f = triangleData[3];
  dataf[1] = f[0];
  dataf[13] = f[1];
  dataf[25] = f[2];

  f = triangleData[6];
  dataf[2] = f[0];
  dataf[14] = f[1];
  dataf[26] = f[2];

  f = triangleData[9];
  dataf[3] = f[0];
  dataf[15] = f[1];
  dataf[27] = f[2];
}

#if CULLING_ENGINE_ENABLE_OCCLUDER_OCCLUDEE_DEBUG

/*
 * Counts active SIMD primitive lanes from the sign bits in primitiveValid. This
 * is used only by debug counters and assertions.
 */
static int GetValidPrimitiveNum(__m128 primitiveValid) {
  uint32_t* v = (uint32_t*)&primitiveValid;
  return (v[0] >> 31) + (v[1] >> 31) + (v[2] >> 31) + (v[3] >> 31);
}

#endif  // End of CULLING_ENGINE_ENABLE_OCCLUDER_OCCLUDEE_DEBUG

/*
 * Reciprocal helper that uses full division instead of approximate reciprocal.
 * Near-clipping paths prefer the extra precision for stable edge interpolation.
 */
static inline __m128 _mm_rcp_ps_div(__m128 _A) {
  return _mm_div_ps(_mm_set1_ps(1.0), _A);
}

/*
 * Rasterizes a batch of up to four convex quads. The template flag controls
 * pixel-AABB clipping, which is normally enabled to keep block masks correct at
 * primitive boundaries.
 */
template <bool bPixelAABBClippingQuad>
void Rasterizer::DrawQuad(__m128* x, __m128* y, __m128* invW, __m128* W,
                          __m128 primitiveValid, __m128* edgeNormalsX,
                          __m128* edgeNormalsY, __m128* areas) {
  __m128 minFx, minFy, maxFx, maxFy;

  // Standard bounding box inclusion
  minFx = _mm_min_ps(_mm_min_ps(x[0], x[1]), _mm_min_ps(x[2], x[3]));
  maxFx = _mm_max_ps(_mm_max_ps(x[0], x[1]), _mm_max_ps(x[2], x[3]));

  minFy = _mm_min_ps(_mm_min_ps(y[0], y[1]), _mm_min_ps(y[2], y[3]));
  maxFy = _mm_max_ps(_mm_max_ps(y[0], y[1]), _mm_max_ps(y[2], y[3]));

  // Clamp upper bound and unpack
  __m128i bounds[4];

  bounds[0] =
      _mm_max_epi32(_mm_cvttps_epi32(minFx), _mm_set1_epi32(mBlockWidthMin));
  bounds[1] =
      _mm_min_epi32(_mm_cvttps_epi32(maxFx), _mm_set1_epi32(mBlockWidthMax));
  bounds[2] = _mm_max_epi32(_mm_cvttps_epi32(minFy), _mm_setzero_si128());
  bounds[3] =
      _mm_min_epi32(_mm_cvttps_epi32(maxFy), _mm_set1_epi32(m_blocksYMinusOne));

  // Check overlap between bounding box and frustum
  __m128 isInFrustum = _mm_castsi128_ps(
      _mm_and_si128(_mm_cmple_epi32_soc(bounds[0], bounds[1]),
                    _mm_cmple_epi32_soc(bounds[2], bounds[3])));
  primitiveValid = _mm_and_ps(isInFrustum, primitiveValid);

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    int temp = GetValidPrimitiveNum(primitiveValid);
    this->DebugData[kPrimitiveFrustumCull] +=
        (DebugData[kPrimitiveValidNum] - temp) * 2;
    DebugData[kPrimitiveValidNum] = temp;
  });

  uint32_t validMask = _mm_movemask_ps(primitiveValid);
  if (validMask == 0) {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      this->DebugData[kP4FrustumCull]++;
      this->DebugData[kP4QuadFrustumCull]++;
    });

    return;
  }

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { this->DebugData[kP4QuadFrustumPass]++; });

  // Compute Z from linear relation with 1/W
  __m128 z[4];
  __m128 c0 = mOccluderCache.c0;
  __m128 c1 = mOccluderCache.c1;
  z[0] = _mm_fmadd_ps(invW[0], c1, c0);
  z[1] = _mm_fmadd_ps(invW[1], c1, c0);
  z[2] = _mm_fmadd_ps(invW[2], c1, c0);
  z[3] = _mm_fmadd_ps(invW[3], c1, c0);

  __m128 maxZ = _mm_max_ps(_mm_max_ps(z[0], z[1]), _mm_max_ps(z[2], z[3]));

  __m128i maxZi = PackPositiveBatchZ(maxZ);

  uint32_t* depthBounds = (uint32_t*)&maxZi;

  // we could use hiZ to do a quick culling.

  // quick cull check
  if (CULL_FEATURE_HizPrimitiveCull) {
    uint32_t alivePrimitive = 0;
    uint16_t* pHiZBuffer = m_pHiz;
    uint32_t pValidIdx = mAliveIdxMask[validMask];
    do {
      // Move index and mask to next set bit
      uint32_t primitiveIdx = pValidIdx & 3;
      pValidIdx >>= 2;

      // Extract and prepare per-primitive data
      uint16_t primitiveMaxZ = depthBounds[primitiveIdx];
      // do a quick check here to reject occluded primitive
      {
        uint32_t* boundData = ((uint32_t*)bounds) + primitiveIdx;
        uint32_t blockMinX = boundData[0];
        uint32_t blockMaxX = boundData[4];
        const uint32_t blockMinY = boundData[8];
        uint32_t blockMaxY = boundData[12];

        uint16_t* pOffsetHiZ = pHiZBuffer + (m_blocksX * blockMinY + blockMinX);

        // reduce 1 and then blockRangeY = BlockMax-BlockMin, then loop  [0,
        // BlockMax - blockMin] blockRangeY--; //bounds[1] =
        // _mm_add_epi32(_mm_sub_epi32(bounds[1], bounds[0]),
        // _mm_set1_epi32(1)); blockRangeX--; //bounds[3] =
        // _mm_add_epi32(_mm_sub_epi32(bounds[3], bounds[2]),
        // _mm_set1_epi32(1));
        int blockRangeX = blockMaxX - blockMinX;
        __m128i primitiveMaxZVi = _mm_set1_epi16(primitiveMaxZ);

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kBlockDoWhileIfSave]++; });

        uint32_t NextBlockY = blockMinY;
        do {
          uint32_t blockY = NextBlockY++;
          uint16_t* pBlockRowHiZ = pOffsetHiZ;
          pOffsetHiZ += m_blocksX;

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockDoWhileIfSave]++; });

          int32_t NextRowRangeX = blockRangeX;
          do {
            int32_t rowRangeX = NextRowRangeX;
            NextRowRangeX -= 8;

            // Load HiZ for 8 blocks at once - note we're possibly reading
            // out-of-bounds here; but it doesn't affect correctness if we test
            // more blocks than actually covered
            __m128i hiZblob =
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(pBlockRowHiZ));
            __m128i cmpResult = _mm_cmplt_epu16_soc(hiZblob, primitiveMaxZVi);
            uint64_t* r64 = (uint64_t*)&cmpResult;
            uint64_t result = r64[0] & r64[1];

            if (result != -1) {
              int32_t byteBlockCheck = std::min(7, rowRangeX);
              int32_t bit = byteBlockCheck >> 2;
              int32_t offset = byteBlockCheck & 3;
              r64[bit] <<= (3 ^ offset) << 4;

              uint64_t mask1 = -bit;
              r64[1] &= mask1;

              result = r64[0] | r64[1];

              if (result == 0) {
                pBlockRowHiZ += 8;
                continue;
              }
            }

            boundData[8] = blockY;

            alivePrimitive |= 1 << primitiveIdx;
            blockMaxY = 0;  // reset blockMaxY  to 0 to force do while exit

            break;
          } while (NextRowRangeX >= 0);
        } while (NextBlockY <= blockMaxY);

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          uint32_t checkMask = 1 << primitiveIdx;
          if ((alivePrimitive & checkMask) == 0) {
            DebugData[kPrimitiveEarlyHizCull]++;
            DebugData[kPrimitiveValidNum]--;
          }
        });
      }

    } while (pValidIdx != 0);

    if (alivePrimitive == 0) {
      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
        DebugData[kP4EarlyHizCull]++;
        assert(DebugData[kPrimitiveValidNum] == 0);
      });

      return;
    }

    validMask = mAliveIdxMask[alivePrimitive];
  }

  if (bPixelAABBClippingQuad) {
    auto scale8 = _mm_set1_ps(8);
    __m128i mask = _mm_set1_epi32((uint32_t(-1) << 16) | 7);

    __m128i pixel = _mm_cvttps_epi32(_mm_mul_ps(minFx, scale8));
    // make bit 16~31 store block value for each int component
    // make bit 0~15 store block pixel remainder(0~7)
    mPrimitiveBoundaryClip->PrimitivePixelBounds[0] =
        _mm_and_si128(mask, _mm_or_si128(_mm_slli_epi32(pixel, 13), pixel));
    pixel = _mm_cvttps_epi32(_mm_mul_ps(maxFx, scale8));
    mPrimitiveBoundaryClip->PrimitivePixelBounds[1] =
        _mm_and_si128(mask, _mm_or_si128(_mm_slli_epi32(pixel, 13), pixel));
    pixel = _mm_cvttps_epi32(_mm_mul_ps(minFy, scale8));
    mPrimitiveBoundaryClip->PrimitivePixelBounds[2] =
        _mm_and_si128(mask, _mm_or_si128(_mm_slli_epi32(pixel, 13), pixel));
    pixel = _mm_cvttps_epi32(_mm_mul_ps(maxFy, scale8));
    mPrimitiveBoundaryClip->PrimitivePixelBounds[3] =
        _mm_and_si128(mask, _mm_or_si128(_mm_slli_epi32(pixel, 13), pixel));
  }

  if (this->mDebugRenderMode) {
    HandleDrawMode<4>(x, y, z, validMask);
  }

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    DebugData[kP4EarlyHizCullPass]++;
    this->DebugData[kBatchQuad4Rasterized]++;
  });

  // Compute screen space depth plane
  __m128 depthPlane[4];

  __m128 maxArea = _mm_max_ps(areas[1], areas[0]);
  __m128 greaterArea = _mm_cmpeq_ps(maxArea, areas[1]);

  //__m128 invArea = _mm_rcp_ps(maxArea);
  __m128 invArea = _mm_rcp_ps_div(maxArea);

  __m128 z12 = _mm_sub_ps(z[1], z[2]);
  __m128 z20 = _mm_sub_ps(z[2], z[0]);
  __m128 z30 = _mm_sub_ps(z[3], z[0]);

  // delay the calculation of edgeNormalX4, edgeNormalY4
  __m128 edgeNormalsX4 = _mm_sub_ps(y[0], y[2]);
  __m128 edgeNormalsY4 = _mm_sub_ps(x[2], x[0]);

  // Depth delta X/Y - select the derivatives from the triangle with the greater
  // area, which is numerically more stable
  depthPlane[1] = _mm_mul_ps(
      invArea,
      _mm_blendv_ps(
          _mm_fmsub_ps(z20, edgeNormalsX[1], _mm_mul_ps(z12, edgeNormalsX4)),
          _mm_fnmadd_ps(z20, edgeNormalsX[3], _mm_mul_ps(z30, edgeNormalsX4)),
          greaterArea));
  depthPlane[2] = _mm_mul_ps(
      invArea,
      _mm_blendv_ps(
          _mm_fmsub_ps(z20, edgeNormalsY[1], _mm_mul_ps(z12, edgeNormalsY4)),
          _mm_fnmadd_ps(z20, edgeNormalsY[3], _mm_mul_ps(z30, edgeNormalsY4)),
          greaterArea));

  if (bDepthAtCenterOptimization == false) {
    // Depth at center of first pixel
    auto one16 = _mm_set1_ps(1.0f / 16.0f);  // load into register once
    __m128 refX = _mm_sub_ps(one16, x[0]);
    __m128 refY = _mm_sub_ps(one16, y[0]);
    depthPlane[0] = _mm_fmadd_ps(refX, depthPlane[1],
                                 _mm_fmadd_ps(refY, depthPlane[2], z[0]));
  } else {
    // Depth at center of first pixel. Optimization. Save One _mm_sub_ps X
    // horizontally
    // allow vertical half pixel error. This would save 1 _mm_sub_ps Y
    // vertically and avoid load 1/16 into memory
    depthPlane[0] = _mm_sub_ps(
        z[0],
        _mm_fmadd_ps(x[0], depthPlane[1], _mm_mul_ps(y[0], depthPlane[2])));
  }

  // Normalize edge equations for lookup
  __m128 invLen[4];
  // Quantize slopes
  __m128i slopeLookups[4];
  NormalizeEdgeAndQuantizeSlope(edgeNormalsX[0], edgeNormalsY[0], invLen[0],
                                slopeLookups[0]);
  NormalizeEdgeAndQuantizeSlope(edgeNormalsX[1], edgeNormalsY[1], invLen[1],
                                slopeLookups[1]);
  NormalizeEdgeAndQuantizeSlope(edgeNormalsX[2], edgeNormalsY[2], invLen[2],
                                slopeLookups[2]);
  NormalizeEdgeAndQuantizeSlope(edgeNormalsX[3], edgeNormalsY[3], invLen[3],
                                slopeLookups[3]);

  __m128 edgeOffsets[4];
  // Important not to use FMA here to ensure identical results between
  // neighboring edges
  edgeOffsets[0] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[1], y[0]), _mm_mul_ps(y[1], x[0])), invLen[0]);
  edgeOffsets[1] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[2], y[1]), _mm_mul_ps(y[2], x[1])), invLen[1]);
  edgeOffsets[2] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[3], y[2]), _mm_mul_ps(y[3], x[2])), invLen[2]);
  edgeOffsets[3] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[0], y[3]), _mm_mul_ps(y[0], x[3])), invLen[3]);

  // Fetch data pointers since we'll manually strength-reduce memory arithmetic
  uint16_t* pHiZBuffer = m_pHiz;

  do {
    uint32_t primitiveIdx = validMask & 3;
    validMask >>= 2;

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
        { this->DebugData[kPrimitiveRasterizedQuadNum]++; });

    uint32_t* slopeLookup = ((uint32_t*)&slopeLookups) + primitiveIdx;

    const uint64_t* pRow0 = m_pMaskTable + slopeLookup[0];
    const uint64_t* pRow1 = m_pMaskTable + slopeLookup[4];
    const uint64_t* pRow2 = m_pMaskTable + slopeLookup[8];
    const uint64_t* pRow3 = m_pMaskTable + slopeLookup[12];

    // Extract and prepare per-primitive data
    uint32_t primitiveMaxZf = depthBounds[primitiveIdx];
    uint16_t primitiveMaxZ = (uint16_t)primitiveMaxZf;

    if (SupportDepthTill65K) {
      primitiveMaxZf |= 65536;
      primitiveMaxZf <<= 11;
    } else {
      primitiveMaxZf <<= 12;
    }

    float* depthPlaneData = ((float*)depthPlane) + primitiveIdx;

    float slope = depthPlaneData[8];

    int xIncrease = (int)(depthPlaneData[4] > 0);
    int yIncrease = (int)(slope > 0);
    // data: btmLeft 0 btmRight 0 topleft 0 topright 0
    // int maxBlockIdx = ((xIncrease << 1) | yIncrease) << 1;
    int maxBlockIdx = (xIncrease + yIncrease * 2) << 1;
    int minBlockIdx = 6 ^ maxBlockIdx;

    //***********************************************************

    __m128 depthBlockDelta = _mm_set1_ps(slope);

    // aggressive approach
    __m128 depthRowDeltaBtm = _mm_setzero_ps();
    depthRowDeltaBtm =
        _mm_min_ps(_mm_set1_ps(slope * 0.375f), depthRowDeltaBtm);

    __m128 depthDx = _mm_set1_ps(depthPlaneData[4]);
    __m128 depthLeftBase;
    if (VRS_X4Y4_Optimzation) {
      // 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625,
      if (depthPlaneData[4] > 0)
        depthLeftBase = _mm_fmadd_ps(depthDx, _mm_setr_ps(0, .5f, 00, 0.5f),
                                     _mm_set1_ps(depthPlaneData[0]));
      else
        depthLeftBase =
            _mm_fmadd_ps(depthDx, _mm_setr_ps(0.375f, 0.875f, 0.375f, 0.875f),
                         _mm_set1_ps(depthPlaneData[0]));
      float halfSlope = slope * 0.5f;
      depthLeftBase =
          _mm_add_ps(depthLeftBase, _mm_setr_ps(0, 0, halfSlope, halfSlope));
    } else {
      depthLeftBase =
          _mm_fmadd_ps(depthDx, _mm_setr_ps(0.0f, 0.125f, 0.25f, 0.375f),
                       _mm_set1_ps(depthPlaneData[0]));
    }

    uint32_t* boundData = ((uint32_t*)bounds) + primitiveIdx;
    uint32_t blockMinX = boundData[0];
    uint32_t blockMaxX = boundData[4];
    const uint32_t blockMinY = boundData[8];
    uint32_t blockMaxY = boundData[12];

    float* edgeNormalsXf = (float*)edgeNormalsX + primitiveIdx;
    float* edgeNormalsYf = (float*)edgeNormalsY + primitiveIdx;
    float* edgeOffsetsf = (float*)edgeOffsets + primitiveIdx;

    __m128 edgeNormalsXP = _mm_setr_ps(edgeNormalsXf[0], edgeNormalsXf[4],
                                       edgeNormalsXf[8], edgeNormalsXf[12]);
    __m128 edgeNormalsYP = _mm_setr_ps(edgeNormalsYf[0], edgeNormalsYf[4],
                                       edgeNormalsYf[8], edgeNormalsYf[12]);
    __m128 edgeOffsetsP = _mm_setr_ps(edgeOffsetsf[0], edgeOffsetsf[4],
                                      edgeOffsetsf[8], edgeOffsetsf[12]);

    // delay calculation of edgeNormalsX edgeNormalsY
    //_mm_add_ps(edgeNormalsX[primitiveIdx], edgeNormalsY[primitiveIdx]),
    //_mm_set1_ps(0.5f) is the central point of 8x8 block
    __m128 edgeOffset = _mm_fmadd_ps(_mm_add_ps(edgeNormalsXP, edgeNormalsYP),
                                     _mm_set1_ps(0.5f), edgeOffsetsP);
    __m128 edge_mul = _mm_set1_ps(OFFSET_mul);
    edgeOffset = _mm_fmadd_ps(edgeOffset, edge_mul, _mm_set1_ps(OFFSET_add));

    __m128 edgeNormalX = _mm_mul_ps(edgeNormalsXP, edge_mul);
    __m128 edgeNormalY = _mm_mul_ps(edgeNormalsYP, edge_mul);

    const uint32_t blocksX = m_blocksX;

    uint32_t startYBlocks = blocksX * blockMinY;
    uint16_t* pOffsetHiZ = pHiZBuffer + startYBlocks;

    uint64_t* outblockRowData = m_pDepthBuffer + startYBlocks * PairBlockNum;

    __m128 rowDepthLeftBtmOffset = _mm_add_ps(depthLeftBase, depthRowDeltaBtm);

    if (bPixelAABBClippingQuad) {
      mPrimitiveBoundaryClip->UpdatePixelAABBData(primitiveIdx);
    }

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
        { DebugData[kBlockTotalPrimitives]++; });

    uint32_t blockY = blockMinY;
    uint32_t anythingDraw = 0;
    __m128 edgeOffsetMin =
        _mm_fmadd_ps(edgeNormalX, _mm_set1_ps(float(blockMinX)), edgeOffset);
    while (true) {
      __m128 blockYf = _mm_set1_ps(float(blockY));
      __m128 rowDepthLeftBtm =
          _mm_fmadd_ps(depthBlockDelta, blockYf, rowDepthLeftBtmOffset);

      __m128 offset = _mm_fmadd_ps(edgeNormalY, blockYf, edgeOffsetMin);

      uint32_t blockRowOffset = 0;
      for (uint32_t blockX = blockMinX; blockX <= blockMaxX;
           blockX++, offset = _mm_add_ps(edgeNormalX, offset)) {
        __m128i lookup = _mm_cvttps_epi32(offset);

        lookup = _mm_max_epi32(lookup, _mm_setzero_si128());

        int32_t* lookIdx = (int32_t*)&lookup;
        int32_t idxOr = lookIdx[0] | lookIdx[1] | lookIdx[2] | lookIdx[3];
        if (idxOr > 63) {
          // Convex Optimization 0: YesNo optimization. Stop if Block state from
          // see to not see
          if (bConvexOptimization) {
            blockX |= blockRowOffset;
          }
          continue;
        }
        if (bConvexOptimization) {
          blockRowOffset = 65536;
        }
        uint16_t* pBlockRowHiZ = pOffsetHiZ + blockX;
        if (pBlockRowHiZ[0] >= primitiveMaxZ) {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockPrimitiveMaxLessThanMinCull]++; });

          continue;
        }

        uint64_t blockMask = -1;
        if (idxOr != 0) {
          blockMask = pRow0[lookIdx[0]];
          blockMask &= pRow1[lookIdx[1]];
          blockMask &= pRow2[lookIdx[2]];
          blockMask &= pRow3[lookIdx[3]];

          // No pixels covered => skip block
          if (!blockMask) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                { DebugData[kBlockMaskJointZeroCull]++; });

            continue;
          }
        }

        // drawQuad routine
        if (VRS_X4Y4_Optimzation) {
          __m128i rowDepthLeft = _mm_castps_si128(_mm_fmadd_ps(
              depthDx, _mm_set1_ps((float)blockX), rowDepthLeftBtm));

          rowDepthLeft = _mm_max_epi32(
              rowDepthLeft, _mm_set1_epi32(MIN_PIXEL_DEPTH_FLOAT_INT));
          rowDepthLeft =
              _mm_min_epi32(rowDepthLeft, _mm_set1_epi32(primitiveMaxZf));

          __m128i depthData = PackDepthPremultipliedVRS12Fast(rowDepthLeft);
          uint16_t* depth16 = (uint16_t*)&depthData;

          uint16_t maxBlockDepth = depth16[maxBlockIdx];
          if (maxBlockDepth > pBlockRowHiZ[0]) {
            uint64_t* outBlockData = outblockRowData + blockX * PairBlockNum;

            uint32_t* depth32 = (uint32_t*)depth16;
            if (PairBlockNum <= CheckerBoardVizMaskApproach) {
              if (blockMask != -1) {
                if (bPixelAABBClippingQuad) {
                  blockMask &=
                      mPrimitiveBoundaryClip->GetPixelAABBMask(blockX, blockY);
                  if (blockMask == 0) {
                    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                        { DebugData[kBlockAabbClipToZero]++; });

                    continue;
                  }
                }

                CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                  DebugData[kBlockRenderPartial]++;
                  DebugData[kBlockRenderTotal]++;
                });

                if (PairBlockNum == PureCheckerBoardApproach) {
                  __m128i* out = (__m128i*)outBlockData;
                  UpdateBlockMSCBPartial(depth32, blockMask, out, nullptr,
                                         pBlockRowHiZ, maxBlockDepth);
                } else {
                  int bit = blockX & 1;
                  __m128i* out = GetDepthData(outBlockData, bit);
                  uint64_t* maskData = GetMaskData(outBlockData, bit);

                  UpdateBlockMSCBPartial(depth32, blockMask, out, maskData,
                                         pBlockRowHiZ, maxBlockDepth);
                }
              } else  // full block update
              {
                mUpdateAnyBlock = true;

                CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                  DebugData[kBlockRenderFull]++;
                  DebugData[kBlockRenderTotal]++;
                  DebugData[kBlockMinUseOne]++;
                });

                __m128i* out = nullptr;
                if (PairBlockNum == CheckerBoardVizMaskApproach) {
                  int bit = blockX & 1;
                  out = GetDepthData(outBlockData, bit);
                  uint64_t* maskData = GetMaskData(outBlockData, bit);
                  maskData[0] = -1;
                } else {
                  out = (__m128i*)outBlockData;
                }

                uint16_t minBlockDepth = depth16[minBlockIdx];
                // All pixels covered => skip edge tests

                uint16_t* pBlockRowHiZMax = pBlockRowHiZ + m_HizBufferSize;
                if (minBlockDepth >=
                    pBlockRowHiZMax[0])  // full block update, min is larger
                                         // than exist max
                {
                  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                    DebugData[kBlockRenderInitial]++;
                    DebugData[kBlockRenderInitialFull]++;
                  });

                  __m128i depthBottom = _mm_setr_epi32(depth32[0], depth32[0],
                                                       depth32[1], depth32[1]);
                  __m128i depthTop = _mm_setr_epi32(depth32[2], depth32[2],
                                                    depth32[3], depth32[3]);
                  out[0] = depthBottom;
                  out[1] = depthBottom;
                  out[2] = depthTop;
                  out[3] = depthTop;
                  pBlockRowHiZ[0] = minBlockDepth;

                  pBlockRowHiZ[m_HizBufferSize] =
                      maxBlockDepth;  // update max hiz
                } else {
                  __m128i depthBottom = _mm_setr_epi32(depth32[0], depth32[0],
                                                       depth32[1], depth32[1]);
                  __m128i depthTop = _mm_setr_epi32(depth32[2], depth32[2],
                                                    depth32[3], depth32[3]);
                  out[0] = _mm_max_epu16(out[0], depthBottom);
                  out[1] = _mm_max_epu16(out[1], depthBottom);
                  out[2] = _mm_max_epu16(out[2], depthTop);
                  out[3] = _mm_max_epu16(out[3], depthTop);

                  pBlockRowHiZ[0] =
                      std::max<uint16_t>(minBlockDepth, pBlockRowHiZ[0]);
                  pBlockRowHiZMax[0] =
                      std::max<uint16_t>(maxBlockDepth, pBlockRowHiZMax[0]);
                }
              }
            } else if (PairBlockNum == FullBlockApproach) {
#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
              if (bPixelAABBClippingQuad) {
                if (blockMask != -1 && bPixelAABBClipping) {
                  blockMask &=
                      mPrimitiveBoundaryClip->GetPixelAABBMask(blockX, blockY);
                }
              }
              __m128i depthRows[2];
              depthRows[0] = _mm_setr_epi32(depth32[0], depth32[0], depth32[1],
                                            depth32[1]);
              depthRows[1] = _mm_setr_epi32(depth32[2], depth32[2], depth32[3],
                                            depth32[3]);
              UpdateBlock(depthRows, blockMask, (__m128i*)(outBlockData),
                          pBlockRowHiZ);
#endif
            }
          } else {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                { DebugData[kBlockMaxLessThanMinCull]++; });
          }

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
          if (bDumpBlockColumnImage) {
            uint64_t* outBlockData = outblockRowData + blockX * PairBlockNum;
            uint64_t* maskData = GetMaskData(outBlockData, blockX & 1);

            uint64_t t0 = pRow0[lookIdx[0]];
            uint64_t t1 = pRow1[lookIdx[1]];
            uint64_t t2 = pRow2[lookIdx[2]];
            DumpColumnBlock(t0, t1, t2, primitiveMaxZ, maskData[0]);
          }
#endif
        } else {
#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
          __m128 depthDxHalf = _mm_set1_ps(depthPlaneData[4] * 0.5f);
          __m128 lineDepthLeft = _mm_fmadd_ps(
              depthBlockDelta, _mm_set1_ps(float(blockY)), depthLeftBase);
          __m128 rowDepthLeft =
              _mm_fmadd_ps(depthDx, _mm_set1_ps((float)blockX), lineDepthLeft);
          __m128 rowDepthRight = _mm_add_ps(depthDxHalf, rowDepthLeft);

          if (blockMask != -1 && bPixelAABBClippingQuad) {
            blockMask &=
                mPrimitiveBoundaryClip->GetPixelAABBMask(blockX, blockY);
          }

          __m128 depthRowDelta = _mm_set1_ps(slope * 0.125f);

          uint64_t* outBlockData = outblockRowData + blockX * PairBlockNum;
          UpdateBlockWithMaxZ(rowDepthLeft, rowDepthRight, blockMask,
                              depthRowDelta, _mm_set1_epi32(primitiveMaxZf),
                              (__m128i*)outBlockData, pBlockRowHiZ);
#endif
        }
      }
      if (blockY >= blockMaxY) {
        break;
      }
      // from draw to undraw
      if (anythingDraw > blockRowOffset) {
        break;
      }
      anythingDraw = blockRowOffset;
      blockY++;
      pOffsetHiZ += blocksX;
      outblockRowData += m_blocksXFullDataRows;
    }
  } while (validMask > 0);
}

/*
 * Main SIMD rasterization loop for raw, baked, compressed, and near-clipped
 * occluder variants. RASTERIZE_CONFIG resolves storage layout, clipping, super
 * compression, and back-face behavior at compile time.
 */
template <int RASTERIZE_CONFIG>
void Rasterizer::Rasterize(CullingEngine::OccluderMesh& raw) {
  constexpr bool PrimitiveDataCompressed = RASTERIZE_CONFIG & 1;
  constexpr bool possiblyNearClipped = RASTERIZE_CONFIG & 2;
  constexpr bool bBackFaceCulling = (RASTERIZE_CONFIG & 8);
  constexpr bool bPlanarMesh = false;
  constexpr bool bSuperCompressed = RASTERIZE_CONFIG & 4;

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    DebugData[kOccluderRasterized]++;
    ////use to select terrain to debug
    // if (DebugData[kOccluderRasterized] != 72)
    //	return;

    DebugData[kRasterizedOccluderTotalTriangles] += raw.TriangleBatchIdxNum / 3;
    DebugData[kRasterizedOccluderTotalVertices] += raw.VerticesNum;
  });

  __m128* mat = mOccluderCache.mat;
  if (PrimitiveDataCompressed) {
    auto scale = _mm_set1_ps(1.0f / 65535.0f);
    mat[0] = _mm_mul_ps(mat[0], scale);
    mat[1] = _mm_mul_ps(mat[1], scale);
    mat[2] = _mm_mul_ps(mat[2], scale);
  }

  if (bFastSetUpMatOp == false) {
    // *****_MM_TRANSPOSE4_PS*****
    _MM_TRANSPOSE4_PS(mat[0], mat[1], mat[2], mat[3]);

    __m128 Za = _mm_shuffle_ps_single_index(mat[2], 3);
    __m128 Zb = _mm_sum4_ps_soc(mat[2]);

    __m128 Wa = _mm_shuffle_ps_single_index(mat[3], 3);
    __m128 Wb = _mm_sum4_ps_soc(mat[3]);

    __m128 c0 = _mm_div_ps(_mm_sub_ps(Za, Zb), _mm_sub_ps(Wa, Wb));
    // DC2: degenerate case 2
    // add zero check to remove case of Wa = Wb. This would eliminate degenerate
    // case..
    __m128 zero_mask = _mm_cmpneq_ps(Wa, Wb);
    c0 = _mm_and_ps(c0, zero_mask);

    mOccluderCache.c1 = _mm_fnmadd_ps(c0, Wa, Za);
    mOccluderCache.NegativeC1 = _mm_negate_ps_soc(mOccluderCache.c1);
    mOccluderCache.c0 = c0;

    auto one8th = _mm_set1_ps(0.125f);
    mat[0] = _mm_mul_ps(mat[0], one8th);  // scale down by 8

    mat[1] = _mm_mul_ps(mat[1], one8th);  // scale down by 8
  } else {
    float* matF = (float*)mat;
    __m128 matT0 = _mm_setr_ps(matF[0], matF[4], matF[8], matF[12]);
    __m128 matT1 = _mm_setr_ps(matF[1], matF[4 + 1], matF[8 + 1], matF[12 + 1]);
    __m128 matT3 = _mm_setr_ps(matF[3], matF[4 + 3], matF[8 + 3], matF[12 + 3]);

    float za_bf = matF[0 * 4 + 2] + matF[1 * 4 + 2] + matF[2 * 4 + 2];
    if (za_bf ==
        0)  // most of Occluders should have no scale which means only R|T
    {
      mOccluderCache.c1 = _mm_set1_ps(
          matF[3 * 4 + 2]);  // _mm_shuffle_ps_single_index(mat[2], 3);
      mOccluderCache.NegativeC1 = _mm_set1_ps(-matF[3 * 4 + 2]);
      mOccluderCache.c0 = _mm_set1_ps(0);
    } else {
      //_MM_TRANSPOSE4_PS(mat[0], mat[1], mat[2], mat[3]);
      __m128 Wa = _mm_set1_ps(
          matF[3 * 4 + 3]);  // _mm_shuffle_ps_single_index(mat[3], 3);
      __m128 Za = _mm_set1_ps(
          matF[3 * 4 + 2]);  //_mm_shuffle_ps_single_index(mat[2], 3);
      //__m128 Zb = _mm_set1_ps(matF[0 * 4 + 2] + matF[1 * 4 + 2] + matF[2 * 4 +
      // 2] + matF[3 * 4 + 2]); //_mm_sum4_ps_soc(mat[2]);
      __m128 Za_b = _mm_set1_ps(za_bf);  //_mm_sum4_ps_soc(mat[2]);

      //__m128 Wb = _mm_set1_ps(matF[0 * 4 + 3] + matF[1 * 4 + 3] + matF[2 * 4 +
      // 3] + matF[3 * 4 + 3]); //_mm_sum4_ps_soc(mat[3]);
      __m128 Wa_b = _mm_set1_ps(matF[0 * 4 + 3] + matF[1 * 4 + 3] +
                                matF[2 * 4 + 3]);  //_mm_sum4_ps_soc(mat[3]);
      __m128 c0 = _mm_div_ps(Za_b, Wa_b);
      // DC2: degenerate case 2
      // add zero check to remove case of Wa = Wb. This would eliminate
      // degenerate case..
      __m128 zero_mask = _mm_cmpneq_ps(Wa_b, _mm_setzero_ps());
      c0 = _mm_and_ps(c0, zero_mask);

      mOccluderCache.c1 = _mm_fnmadd_ps(c0, Wa, Za);
      mOccluderCache.c0 = c0;
      mOccluderCache.NegativeC1 = _mm_negate_ps_soc(mOccluderCache.c1);
    }

    auto one8th = _mm_set1_ps(0.125f);
    mat[0] = _mm_mul_ps(matT0, one8th);  // scale down by 8
    mat[1] = _mm_mul_ps(matT1, one8th);  // scale down by 8
    mat[3] = matT3;
  }

  // *****_MM_SHUFFLE*****
  __m128 mat33;  // = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 3);
  __m128 mat03;  // = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 3);
  __m128 mat13;  // = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 3);

  if (PrimitiveDataCompressed == false) {
    auto c = mOccluderCache.FullMeshMinusRefMinInvExtents;
    mat03 = _mm_sum4_ps_soc(_mm_mul_ps(c, mOccluderCache.mat[0]));
    mat13 = _mm_sum4_ps_soc(_mm_mul_ps(c, mOccluderCache.mat[1]));
    mat33 = _mm_sum4_ps_soc(_mm_mul_ps(c, mOccluderCache.mat[3]));

    /// Xf0 = _mm_fmadd_ps(dataArray[0], b, c);
    __m128 b = mOccluderCache.FullMeshInvExtents;
    mOccluderCache.mat[0] = _mm_mul_ps(mOccluderCache.mat[0], b);
    mOccluderCache.mat[1] = _mm_mul_ps(mOccluderCache.mat[1], b);
    mOccluderCache.mat[3] = _mm_mul_ps(mOccluderCache.mat[3], b);
  } else {
    mat03 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 3);
    mat13 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 3);
    mat33 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 3);
  }

  __m128 dataArray[12];
  const __m128i* vertexData = nullptr;
  uint8_t* pCompressIndices8 = nullptr;
  uint16_t* pCompressIndices = nullptr;
  uint16_t* pCompressVertices = nullptr;
  if (PrimitiveDataCompressed == true) {
    if (bSuperCompressed) {
      pCompressIndices8 = (uint8_t*)raw.Vertices;
      pCompressVertices =
          (uint16_t*)(pCompressIndices8 + raw.QuadSafeBatchNum * 16 +
                      raw.TriangleBatchIdxNum * 12);
    } else if (CullingEngine::bEnableCompressMode) {
      pCompressIndices = (uint16_t*)raw.Vertices;
      pCompressVertices =
          (uint16_t*)(pCompressIndices + raw.QuadSafeBatchNum * 16 +
                      raw.TriangleBatchIdxNum * 12);
    } else {
      vertexData = (__m128i*)raw.Vertices;
    }
  }

  int flip = this->mClockWise != this->mOccluderCache.FlipOccluderFace;

  // swap 0 and 2 in case of flipping
  int faceIdx0 = flip << 1;
  int faceIdx2 = 2 ^ faceIdx0;

  if (PrimitiveDataCompressed && raw.QuadSafeBatchNum > 0) {
    //***************************
    int packetsLeft = raw.QuadSafeBatchNum;
    float* pData = (float*)dataArray;
    do {
      // prepare the 16 tempV
      if (bSuperCompressed) {
        uint16_t* a = pCompressVertices + pCompressIndices8[0];
        uint16_t* b = pCompressVertices + pCompressIndices8[4];
        uint16_t* c = pCompressVertices + pCompressIndices8[8];
        uint16_t* d = pCompressVertices + pCompressIndices8[12];

        // dataArray[0] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0] = a[0];
        pData[1] = b[0];
        pData[2] = c[0];
        pData[3] = d[0];
        // dataArray[4] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16] = a[1];
        pData[17] = b[1];
        pData[18] = c[1];
        pData[19] = d[1];
        // dataArray[8] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32] = a[2];
        pData[33] = b[2];
        pData[34] = c[2];
        pData[35] = d[2];

        a = pCompressVertices + pCompressIndices8[0 + 1];
        b = pCompressVertices + pCompressIndices8[4 + 1];
        c = pCompressVertices + pCompressIndices8[8 + 1];
        d = pCompressVertices + pCompressIndices8[12 + 1];

        // dataArray[3] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 12] = a[0];
        pData[1 + 12] = b[0];
        pData[2 + 12] = c[0];
        pData[3 + 12] = d[0];
        // dataArray[7] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 12] = a[1];
        pData[17 + 12] = b[1];
        pData[18 + 12] = c[1];
        pData[19 + 12] = d[1];
        // dataArray[11] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 12] = a[2];
        pData[33 + 12] = b[2];
        pData[34 + 12] = c[2];
        pData[35 + 12] = d[2];

        a = pCompressVertices + pCompressIndices8[0 + 2];
        b = pCompressVertices + pCompressIndices8[4 + 2];
        c = pCompressVertices + pCompressIndices8[8 + 2];
        d = pCompressVertices + pCompressIndices8[12 + 2];

        // dataArray[2] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 8] = a[0];
        pData[1 + 8] = b[0];
        pData[2 + 8] = c[0];
        pData[3 + 8] = d[0];
        // dataArray[6] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 8] = a[1];
        pData[17 + 8] = b[1];
        pData[18 + 8] = c[1];
        pData[19 + 8] = d[1];
        // dataArray[10] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 8] = a[2];
        pData[33 + 8] = b[2];
        pData[34 + 8] = c[2];
        pData[35 + 8] = d[2];

        a = pCompressVertices + pCompressIndices8[0 + 3];
        b = pCompressVertices + pCompressIndices8[4 + 3];
        c = pCompressVertices + pCompressIndices8[8 + 3];
        d = pCompressVertices + pCompressIndices8[12 + 3];

        // dataArray[1] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 4] = a[0];
        pData[1 + 4] = b[0];
        pData[2 + 4] = c[0];
        pData[3 + 4] = d[0];
        // dataArray[5] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 4] = a[1];
        pData[17 + 4] = b[1];
        pData[18 + 4] = c[1];
        pData[19 + 4] = d[1];
        // dataArray[9] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 4] = a[2];
        pData[33 + 4] = b[2];
        pData[34 + 4] = c[2];
        pData[35 + 4] = d[2];

        pCompressIndices8 += 16;
      } else if (CullingEngine::bEnableCompressMode) {
        //////triData[0] = pCompressVertices + pCompressIndices[0];
        //////triData[1] = pCompressVertices + pCompressIndices[1];
        //////triData[2] = pCompressVertices + pCompressIndices[2];
        //////triData[3] = pCompressVertices + pCompressIndices[3];
        //////triData[4] = pCompressVertices + pCompressIndices[4];
        //////triData[5] = pCompressVertices + pCompressIndices[5];
        //////triData[6] = pCompressVertices + pCompressIndices[6];
        //////triData[7] = pCompressVertices + pCompressIndices[7];
        //////triData[8] = pCompressVertices + pCompressIndices[8];
        //////triData[9] = pCompressVertices + pCompressIndices[9];
        //////triData[10] = pCompressVertices + pCompressIndices[10];
        //////triData[11] = pCompressVertices + pCompressIndices[11];
        //////triData[12] = pCompressVertices + pCompressIndices[12];
        //////triData[13] = pCompressVertices + pCompressIndices[13];
        //////triData[14] = pCompressVertices + pCompressIndices[14];
        //////triData[15] = pCompressVertices + pCompressIndices[15];

        ////////dataArray[0] = _mm_setr_ps(triData[0][0], triData[4][0],
        /// triData[8][0], triData[12][0]);
        ////////dataArray[3] = _mm_setr_ps(triData[0 + 1][0], triData[4 + 1][0],
        /// triData[8 + 1][0], triData[12 + 1][0]);
        ////////dataArray[2] = _mm_setr_ps(triData[0 + 2][0], triData[4 + 2][0],
        /// triData[8 + 2][0], triData[12 + 2][0]);
        ////////dataArray[1] = _mm_setr_ps(triData[0 + 3][0], triData[4 + 3][0],
        /// triData[8 + 3][0], triData[12 + 3][0]);

        ////////dataArray[4] = _mm_setr_ps(triData[0][1], triData[4][1],
        /// triData[8][1], triData[12][1]);
        ////////dataArray[7] = _mm_setr_ps(triData[0 + 1][1], triData[4 + 1][1],
        /// triData[8 + 1][1], triData[12 + 1][1]);
        ////////dataArray[6] = _mm_setr_ps(triData[0 + 2][1], triData[4 + 2][1],
        /// triData[8 + 2][1], triData[12 + 2][1]);
        ////////dataArray[5] = _mm_setr_ps(triData[0 + 3][1], triData[4 + 3][1],
        /// triData[8 + 3][1], triData[12 + 3][1]);

        ////////dataArray[8] = _mm_setr_ps(triData[0][2], triData[4][2],
        /// triData[8][2], triData[12][2]);
        ////////dataArray[11] = _mm_setr_ps(triData[0 + 1][2], triData[4 +
        /// 1][2], triData[8 + 1][2], triData[12 + 1][2]);
        ////////dataArray[10] = _mm_setr_ps(triData[0 + 2][2], triData[4 +
        /// 2][2], triData[8 + 2][2], triData[12 + 2][2]);
        ////////dataArray[9] = _mm_setr_ps(triData[0 + 3][2], triData[4 + 3][2],
        /// triData[8 + 3][2], triData[12 + 3][2]);

        uint16_t* a = pCompressVertices + pCompressIndices[0];
        uint16_t* b = pCompressVertices + pCompressIndices[4];
        uint16_t* c = pCompressVertices + pCompressIndices[8];
        uint16_t* d = pCompressVertices + pCompressIndices[12];

        // dataArray[0] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0] = a[0];
        pData[1] = b[0];
        pData[2] = c[0];
        pData[3] = d[0];
        // dataArray[4] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16] = a[1];
        pData[17] = b[1];
        pData[18] = c[1];
        pData[19] = d[1];
        // dataArray[8] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32] = a[2];
        pData[33] = b[2];
        pData[34] = c[2];
        pData[35] = d[2];

        a = pCompressVertices + pCompressIndices[0 + 1];
        b = pCompressVertices + pCompressIndices[4 + 1];
        c = pCompressVertices + pCompressIndices[8 + 1];
        d = pCompressVertices + pCompressIndices[12 + 1];

        // dataArray[3] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 12] = a[0];
        pData[1 + 12] = b[0];
        pData[2 + 12] = c[0];
        pData[3 + 12] = d[0];
        // dataArray[7] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 12] = a[1];
        pData[17 + 12] = b[1];
        pData[18 + 12] = c[1];
        pData[19 + 12] = d[1];
        // dataArray[11] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 12] = a[2];
        pData[33 + 12] = b[2];
        pData[34 + 12] = c[2];
        pData[35 + 12] = d[2];

        a = pCompressVertices + pCompressIndices[0 + 2];
        b = pCompressVertices + pCompressIndices[4 + 2];
        c = pCompressVertices + pCompressIndices[8 + 2];
        d = pCompressVertices + pCompressIndices[12 + 2];

        // dataArray[2] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 8] = a[0];
        pData[1 + 8] = b[0];
        pData[2 + 8] = c[0];
        pData[3 + 8] = d[0];
        // dataArray[6] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 8] = a[1];
        pData[17 + 8] = b[1];
        pData[18 + 8] = c[1];
        pData[19 + 8] = d[1];
        // dataArray[10] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 8] = a[2];
        pData[33 + 8] = b[2];
        pData[34 + 8] = c[2];
        pData[35 + 8] = d[2];

        a = pCompressVertices + pCompressIndices[0 + 3];
        b = pCompressVertices + pCompressIndices[4 + 3];
        c = pCompressVertices + pCompressIndices[8 + 3];
        d = pCompressVertices + pCompressIndices[12 + 3];

        // dataArray[1] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
        pData[0 + 4] = a[0];
        pData[1 + 4] = b[0];
        pData[2 + 4] = c[0];
        pData[3 + 4] = d[0];
        // dataArray[5] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
        pData[16 + 4] = a[1];
        pData[17 + 4] = b[1];
        pData[18 + 4] = c[1];
        pData[19 + 4] = d[1];
        // dataArray[9] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
        pData[32 + 4] = a[2];
        pData[33 + 4] = b[2];
        pData[34 + 4] = c[2];
        pData[35 + 4] = d[2];

        pCompressIndices += 16;
      } else {
        //*****************************************************************************************************
        // now take 6 m128, each take 0.5, total 4x3
        __m128i X0X1 = vertexData[0];
        __m128i X2X3 = vertexData[1];
        __m128i Y0Y1 = vertexData[2];
        __m128i Y2Y3 = vertexData[3];
        __m128i Z0Z1 = vertexData[4];
        __m128i Z2Z3 = vertexData[5];
        vertexData += 6;

        __m128i mask = _mm_set1_epi32(65535);

        dataArray[0] = _mm_cvtepi32_ps(_mm_srli_epi32(X0X1, 16));
        dataArray[1] = _mm_cvtepi32_ps(_mm_and_si128(X2X3, mask));
        dataArray[2] = _mm_cvtepi32_ps(_mm_srli_epi32(X2X3, 16));
        dataArray[3] = _mm_cvtepi32_ps(_mm_and_si128(X0X1, mask));

        dataArray[4 | 0] = _mm_cvtepi32_ps(_mm_srli_epi32(Y0Y1, 16));
        dataArray[4 | 1] = _mm_cvtepi32_ps(_mm_and_si128(Y2Y3, mask));
        dataArray[4 | 2] = _mm_cvtepi32_ps(_mm_srli_epi32(Y2Y3, 16));
        dataArray[4 | 3] = _mm_cvtepi32_ps(_mm_and_si128(Y0Y1, mask));

        dataArray[8 | 0] = _mm_cvtepi32_ps(_mm_srli_epi32(Z0Z1, 16));
        dataArray[8 | 1] = _mm_cvtepi32_ps(_mm_and_si128(Z2Z3, mask));
        dataArray[8 | 2] = _mm_cvtepi32_ps(_mm_srli_epi32(Z2Z3, 16));
        dataArray[8 | 3] = _mm_cvtepi32_ps(_mm_and_si128(Z0Z1, mask));
        //*****************************************************************************************************
      }
      packetsLeft--;

      __m128 mat30 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 0);
      __m128 mat31 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 1);
      __m128 mat32 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 2);

      __m128 W[4];
      W[faceIdx0] =
          _mm_fmadd_ps(dataArray[0], mat30,
                       _mm_fmadd_ps(dataArray[4], mat31,
                                    _mm_fmadd_ps(dataArray[8], mat32, mat33)));
      W[1] =
          _mm_fmadd_ps(dataArray[1], mat30,
                       _mm_fmadd_ps(dataArray[5], mat31,
                                    _mm_fmadd_ps(dataArray[9], mat32, mat33)));
      W[faceIdx2] =
          _mm_fmadd_ps(dataArray[2], mat30,
                       _mm_fmadd_ps(dataArray[6], mat31,
                                    _mm_fmadd_ps(dataArray[10], mat32, mat33)));
      W[3] =
          _mm_fmadd_ps(dataArray[3], mat30,
                       _mm_fmadd_ps(dataArray[7], mat31,
                                    _mm_fmadd_ps(dataArray[11], mat32, mat33)));

      __m128 primitiveValid = _mm_set1_ps(-0.0f);
      if (possiblyNearClipped) {
        // All W < 0 means fully culled by camera plane
        __m128 W0123 =
            _mm_and_ps(_mm_and_ps(_mm_and_ps(W[0], W[1]), W[2]), W[3]);
        if (_mm_same_sign1_soc(W0123)) {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
            this->DebugData[kP4CameraNearPlaneCull]++;
            this->DebugData[kPrimitiveCameraNearPlaneCull] += 4;

            primitiveValid = _mm_xor_ps(W0123, _mm_set1_ps(-0.0f));
            DebugData[kPrimitiveValidNum] =
                GetValidPrimitiveNum(primitiveValid);
            assert(DebugData[kPrimitiveValidNum] == 0);
          });

          continue;
        }
        primitiveValid = _mm_xor_ps(W0123, primitiveValid);

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          DebugData[kPrimitiveValidNum] = GetValidPrimitiveNum(primitiveValid);
          this->DebugData[kPrimitiveCameraNearPlaneCull] +=
              4 - DebugData[kPrimitiveValidNum];
        });
      } else {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kPrimitiveValidNum] = 4; });
      }

      __m128 X[4], Y[4];

      __m128 mat00 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 0);
      __m128 mat01 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 1);
      __m128 mat02 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 2);
      X[faceIdx0] =
          _mm_fmadd_ps(dataArray[0], mat00,
                       _mm_fmadd_ps(dataArray[4], mat01,
                                    _mm_fmadd_ps(dataArray[8], mat02, mat03)));
      X[1] =
          _mm_fmadd_ps(dataArray[1], mat00,
                       _mm_fmadd_ps(dataArray[5], mat01,
                                    _mm_fmadd_ps(dataArray[9], mat02, mat03)));
      X[faceIdx2] =
          _mm_fmadd_ps(dataArray[2], mat00,
                       _mm_fmadd_ps(dataArray[6], mat01,
                                    _mm_fmadd_ps(dataArray[10], mat02, mat03)));
      X[3] =
          _mm_fmadd_ps(dataArray[3], mat00,
                       _mm_fmadd_ps(dataArray[7], mat01,
                                    _mm_fmadd_ps(dataArray[11], mat02, mat03)));

      __m128 mat10 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 0);
      __m128 mat11 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 1);
      __m128 mat12 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 2);
      Y[faceIdx0] =
          _mm_fmadd_ps(dataArray[0], mat10,
                       _mm_fmadd_ps(dataArray[4], mat11,
                                    _mm_fmadd_ps(dataArray[8], mat12, mat13)));
      Y[1] =
          _mm_fmadd_ps(dataArray[1], mat10,
                       _mm_fmadd_ps(dataArray[5], mat11,
                                    _mm_fmadd_ps(dataArray[9], mat12, mat13)));
      Y[faceIdx2] =
          _mm_fmadd_ps(dataArray[2], mat10,
                       _mm_fmadd_ps(dataArray[6], mat11,
                                    _mm_fmadd_ps(dataArray[10], mat12, mat13)));
      Y[3] =
          _mm_fmadd_ps(dataArray[3], mat10,
                       _mm_fmadd_ps(dataArray[7], mat11,
                                    _mm_fmadd_ps(dataArray[11], mat12, mat13)));

      // Clamp W and invert
      __m128 invW[4];
      // this error might up to 1 pixel for x, and y
      invW[0] = _mm_rcp_ps(W[0]);
      invW[1] = _mm_rcp_ps(W[1]);
      invW[2] = _mm_rcp_ps(W[2]);
      invW[3] = _mm_rcp_ps(W[3]);

      bool realNearClipped = false;
      if (possiblyNearClipped) {
        __m128 allInfront =
            _mm_min_ps(_mm_min_ps(_mm_min_ps(W[0], W[1]), W[2]), W[3]);
        allInfront = _mm_cmplt_ps(allInfront, _mm_set1_ps(0.01f));

        if (!_mm_same_sign0(allInfront))  // near plane clipped
        {
          __m128 lowerBound = _mm_set1_ps(-maxInvW);
          __m128 upperBound = _mm_set1_ps(+maxInvW);

          invW[0] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[0]));
          invW[1] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[1]));
          invW[2] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[2]));
          invW[3] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[3]));

          realNearClipped = true;
        }
      }

      X[0] = _mm_mul_ps(X[0], invW[0]);
      X[1] = _mm_mul_ps(X[1], invW[1]);
      X[2] = _mm_mul_ps(X[2], invW[2]);
      X[3] = _mm_mul_ps(X[3], invW[3]);

      Y[0] = _mm_mul_ps(Y[0], invW[0]);
      Y[1] = _mm_mul_ps(Y[1], invW[1]);
      Y[2] = _mm_mul_ps(Y[2], invW[2]);
      Y[3] = _mm_mul_ps(Y[3], invW[3]);

      if (realNearClipped)  // near plane clipped
      {
        int validMask = _mm_movemask_ps(primitiveValid);
        SplitToTwoTriangles<true, bBackFaceCulling>(X, Y, W, invW,
                                                    primitiveValid, validMask);
        continue;
      }

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { this->DebugData[kP4DrawTriangle]++; });

      __m128 edgeNormalsX[4], edgeNormalsY[4];

      edgeNormalsX[0] = _mm_sub_ps(Y[1], Y[0]);
      edgeNormalsX[1] = _mm_sub_ps(Y[2], Y[1]);
      edgeNormalsX[2] = _mm_sub_ps(Y[3], Y[2]);
      edgeNormalsX[3] = _mm_sub_ps(Y[0], Y[3]);

      edgeNormalsY[0] = _mm_sub_ps(X[0], X[1]);
      edgeNormalsY[1] = _mm_sub_ps(X[1], X[2]);
      edgeNormalsY[2] = _mm_sub_ps(X[2], X[3]);
      edgeNormalsY[3] = _mm_sub_ps(X[3], X[0]);

      __m128 areas[2];
      areas[0] = _mm_fmsub_ps(edgeNormalsX[0], edgeNormalsY[1],
                              _mm_mul_ps(edgeNormalsX[1], edgeNormalsY[0]));
      areas[1] = _mm_fmsub_ps(edgeNormalsX[2], edgeNormalsY[3],
                              _mm_mul_ps(edgeNormalsX[3], edgeNormalsY[2]));

      __m128 minArea = _mm_min_ps(areas[0], areas[1]);
      if (bPlanarMesh ==
          false)  // always set to false as planar case is very rare
      {
        if (bBackFaceCulling == true) {
          // Apply backface culling, reject the quad if both triangles' area < 0
          __m128 anyPositive =
              _mm_cmpgt_ps(_mm_max_ps(areas[0], areas[1]), _mm_set1_ps(0.0f));
          primitiveValid = _mm_and_ps(anyPositive, primitiveValid);

          int validMask = _mm_movemask_ps(primitiveValid);
          if (validMask == 0)  // all negative
          {
            continue;
          }

          // need to sync primitive valid state here
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
            int active = GetValidPrimitiveNum(primitiveValid);
            this->DebugData[kPrimitiveBackfaceCull] +=
                (DebugData[kPrimitiveValidNum] - active) * 2;
            DebugData[kPrimitiveValidNum] = active;
          });

          // check concave scenario: if any triangle of four triangles formed by
          // the four points has negative area the convex property is violated.
          // going for safe way, Triangle Approach
          __m128 area3 =
              _mm_fmsub_ps(edgeNormalsX[1], edgeNormalsY[2],
                           _mm_mul_ps(edgeNormalsX[2], edgeNormalsY[1]));
          __m128 area4 = _mm_sub_ps(_mm_add_ps(areas[0], areas[1]), area3);

          // DC3 degenerate case 3: for the case of any of area1/area2 is zero,
          // normalizeEdge would trigger divide by zero degeneracy
          // fix: treat as concave if any of four triangle is non-positive
          __m128 minArea4 = _mm_min_ps(_mm_min_ps(area4, area3), minArea);
          __m128 concaveQuad = _mm_cmple_ps(minArea4, _mm_set1_ps(0.0001f));

          concaveQuad = _mm_and_ps(
              concaveQuad, primitiveValid);  // any active primitive is concave
          if (_mm_same_sign0(concaveQuad) ==
              false)  // at least one active primitive is concave
          {
            SplitToTwoTriangles<false, bBackFaceCulling>(
                X, Y, W, invW, primitiveValid, validMask);
            continue;
          }
        } else {
          // even in backface cull off state, if any area is negative, go
          // triangle approach
          __m128 area3 =
              _mm_fmsub_ps(edgeNormalsX[1], edgeNormalsY[2],
                           _mm_mul_ps(edgeNormalsX[2], edgeNormalsY[1]));
          __m128 area4 = _mm_sub_ps(_mm_add_ps(areas[0], areas[1]), area3);

          __m128 minArea4 = _mm_min_ps(_mm_min_ps(area4, area3), minArea);
          __m128 concaveQuad = _mm_cmple_ps(minArea4, _mm_set1_ps(0.0001f));

          // in backface cull off mode
          // todo: possible optimization, in case all valid negative, swap could
          // still continue the quad approach
          concaveQuad = _mm_and_ps(
              concaveQuad, primitiveValid);  // any active primitive is concave
          if (_mm_same_sign0(concaveQuad) ==
              false)  // at least one active primitive is concave
          {
            int validMask = _mm_movemask_ps(primitiveValid);
            SplitToTwoTriangles<false, bBackFaceCulling>(
                X, Y, W, invW, primitiveValid, validMask);
            continue;
          }
        }
        // all positive
      } else {
        if (bBackFaceCulling == true) {
          // as it is a plane and not near clipped, as long as any triangle is
          // negative, the whole plane could be culled
          if (_mm_same_sign0(minArea) == false) {
            return;
          }
        } else {
          // as it is a plane and not near clipped, as long as any triangle is
          // negative, the whole order would be swapped
          if (_mm_same_sign0(minArea) == false) {
            std::swap(X[1], X[3]);
            std::swap(Y[1], Y[3]);
            std::swap(W[1], W[3]);
            std::swap(invW[1], invW[3]);
            edgeNormalsX[0] = _mm_sub_ps(Y[1], Y[0]);
            edgeNormalsX[1] = _mm_sub_ps(Y[2], Y[1]);
            edgeNormalsX[2] = _mm_sub_ps(Y[3], Y[2]);
            edgeNormalsX[3] = _mm_sub_ps(Y[0], Y[3]);

            edgeNormalsY[0] = _mm_sub_ps(X[0], X[1]);
            edgeNormalsY[1] = _mm_sub_ps(X[1], X[2]);
            edgeNormalsY[2] = _mm_sub_ps(X[2], X[3]);
            edgeNormalsY[3] = _mm_sub_ps(X[3], X[0]);

            areas[0] =
                _mm_fmsub_ps(edgeNormalsX[0], edgeNormalsY[1],
                             _mm_mul_ps(edgeNormalsX[1], edgeNormalsY[0]));
            areas[1] =
                _mm_fmsub_ps(edgeNormalsX[2], edgeNormalsY[3],
                             _mm_mul_ps(edgeNormalsX[3], edgeNormalsY[2]));
          }
        }

        __m128 anyPositive = _mm_cmpgt_ps(_mm_max_ps(areas[0], areas[1]),
                                          _mm_set1_ps(0.0000001f));
        primitiveValid = _mm_and_ps(primitiveValid, anyPositive);
      }

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { DebugData[kQuadProcessed]++; });

      ////must be positive faces here now
      DrawQuad<true>(X, Y, invW, W, primitiveValid, edgeNormalsX, edgeNormalsY,
                     areas);
      // if (this->mCullAgressiveLevel != 0)
      //{
      //	drawQuad<false>(X, Y, invW, W, primitiveValid, edgeNormalsX,
      // edgeNormalsY, areas);
      // }
      // else
      //{
      //	drawQuad<true>(X, Y, invW, W, primitiveValid, edgeNormalsX,
      // edgeNormalsY, areas);
      // }

    } while (packetsLeft > 0);
  }
#pragma region TrisProcessing

  const float* triangleData[16];
  if (raw.TriangleBatchIdxNum > 0) {
    int triPacketCount = 0;

    const uint16_t* pIndexCurrent = nullptr;
    int faceNum = 0;  // total triangle num for the next round
    if (PrimitiveDataCompressed == true) {
      vertexData = (__m128i*)raw.Vertices + (raw.QuadSafeBatchNum) * 6;
      triPacketCount = raw.TriangleBatchIdxNum;
    } else {
      faceNum = raw.TriangleBatchIdxNum / 3;
      triPacketCount = faceNum >> 2;  // first round P4 number
      pIndexCurrent = raw.Indices;
    }

    do {
      if (PrimitiveDataCompressed == false) {
        if (faceNum >= 4) {
          faceNum = faceNum & 3;
        } else  // the end round
        {
          mIndexBuffer[0] = mIndexBuffer[1] = mIndexBuffer[2] = 0;

          uint16_t* temp = (uint16_t*)mIndexBuffer;
          memcpy(temp, pIndexCurrent, faceNum * (3 * sizeof(uint16_t)));

          pIndexCurrent = temp;
          triPacketCount =
              0;  // 0 or 1 both work, set to zero as zero compare is faster
          faceNum = 0;
        }
      }

      int packetIdx = 0;
      float* pData = (float*)dataArray;
      do {
        //__m128 Xf0, Xf1, Xf2;
        //__m128 Yf0, Yf1, Yf2;
        //__m128 Zf0, Zf1, Zf2;
        if (PrimitiveDataCompressed == false) {
          // prepare the 16 tempV
          triangleData[0] = raw.Vertices + pIndexCurrent[0] * 3;
          triangleData[1] = raw.Vertices + pIndexCurrent[2] * 3;
          triangleData[2] = raw.Vertices + pIndexCurrent[1] * 3;
          triangleData[3] = raw.Vertices + pIndexCurrent[3] * 3;
          triangleData[3 + 1] = raw.Vertices + pIndexCurrent[5] * 3;
          triangleData[3 + 2] = raw.Vertices + pIndexCurrent[4] * 3;
          triangleData[6] = raw.Vertices + pIndexCurrent[6] * 3;
          triangleData[6 + 1] = raw.Vertices + pIndexCurrent[8] * 3;
          triangleData[6 + 2] = raw.Vertices + pIndexCurrent[7] * 3;
          triangleData[9] = raw.Vertices + pIndexCurrent[9] * 3;
          triangleData[9 + 1] = raw.Vertices + pIndexCurrent[11] * 3;
          triangleData[9 + 2] = raw.Vertices + pIndexCurrent[10] * 3;
          pIndexCurrent += 12;

          float* dataf = (float*)dataArray;
          Transpose(triangleData, dataf);
          Transpose(triangleData + 2, dataf + 8);
          Transpose(triangleData + 1, dataf + 4);
        } else if (bSuperCompressed) {
          uint16_t* a = pCompressVertices + pCompressIndices8[0];
          uint16_t* b = pCompressVertices + pCompressIndices8[3];
          uint16_t* c = pCompressVertices + pCompressIndices8[6];
          uint16_t* d = pCompressVertices + pCompressIndices8[9];

          // dataArray[0] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[0] = a[0];
          pData[1] = b[0];
          pData[2] = c[0];
          pData[3] = d[0];
          // dataArray[3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[12] = a[1];
          pData[13] = b[1];
          pData[14] = c[1];
          pData[15] = d[1];
          // dataArray[6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[24] = a[2];
          pData[25] = b[2];
          pData[26] = c[2];
          pData[27] = d[2];

          a = pCompressVertices + pCompressIndices8[1];
          b = pCompressVertices + pCompressIndices8[4];
          c = pCompressVertices + pCompressIndices8[7];
          d = pCompressVertices + pCompressIndices8[10];

          // dataArray[2] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[8] = a[0];
          pData[8 + 1] = b[0];
          pData[8 + 2] = c[0];
          pData[8 + 3] = d[0];
          // dataArray[2 + 3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[20] = a[1];
          pData[20 + 1] = b[1];
          pData[20 + 2] = c[1];
          pData[20 + 3] = d[1];
          // dataArray[2 + 6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[32] = a[2];
          pData[32 + 1] = b[2];
          pData[32 + 2] = c[2];
          pData[32 + 3] = d[2];

          a = pCompressVertices + pCompressIndices8[2];
          b = pCompressVertices + pCompressIndices8[5];
          c = pCompressVertices + pCompressIndices8[8];
          d = pCompressVertices + pCompressIndices8[11];

          // dataArray[1] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[4] = a[0];
          pData[4 + 1] = b[0];
          pData[4 + 2] = c[0];
          pData[4 + 3] = d[0];
          // dataArray[1 + 3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[16] = a[1];
          pData[16 + 1] = b[1];
          pData[16 + 2] = c[1];
          pData[16 + 3] = d[1];
          // dataArray[1 + 6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[28] = a[2];
          pData[28 + 1] = b[2];
          pData[28 + 2] = c[2];
          pData[28 + 3] = d[2];

          pCompressIndices8 += 12;
        } else if (CullingEngine::bEnableCompressMode) {
          ////////super compress mode
          //////triData[0] = pCompressVertices + pCompressIndices[0] ;
          //////triData[idx1] = pCompressVertices + pCompressIndices[1];
          //////triData[idx2] = pCompressVertices + pCompressIndices[2] ;
          //////triData[3] = pCompressVertices + pCompressIndices[3] ;
          //////triData[3 + idx1] = pCompressVertices + pCompressIndices[4] ;
          //////triData[3 + idx2] = pCompressVertices + pCompressIndices[5];
          //////triData[6] = pCompressVertices + pCompressIndices[6];
          //////triData[6 + idx1] = pCompressVertices + pCompressIndices[7];
          //////triData[6 + idx2] = pCompressVertices + pCompressIndices[8];
          //////triData[9] = pCompressVertices + pCompressIndices[9] ;
          //////triData[9 + idx1] = pCompressVertices + pCompressIndices[10];
          //////triData[9 + idx2] = pCompressVertices + pCompressIndices[11];
          //////pCompressIndices += 12;

          //////dataArray[0] = _mm_setr_ps(triData[0][0], triData[3][0],
          /// triData[6][0], triData[9][0]);
          //////dataArray[2] = _mm_setr_ps(triData[0 + 1][0], triData[3 + 1][0],
          /// triData[6 + 1][0], triData[9 + 1][0]);
          //////dataArray[1] = _mm_setr_ps(triData[0 + 2][0], triData[3 + 2][0],
          /// triData[6 + 2][0], triData[9 + 2][0]);

          //////dataArray[3] = _mm_setr_ps(triData[0][1], triData[3][1],
          /// triData[6][1], triData[9][1]);
          //////dataArray[5] = _mm_setr_ps(triData[0 + 1][1], triData[3 + 1][1],
          /// triData[6 + 1][1], triData[9 + 1][1]);
          //////dataArray[4] = _mm_setr_ps(triData[0 + 2][1], triData[3 + 2][1],
          /// triData[6 + 2][1], triData[9 + 2][1]);

          //////dataArray[6] = _mm_setr_ps(triData[0][2], triData[3][2],
          /// triData[6][2], triData[9][2]);
          //////dataArray[8] = _mm_setr_ps(triData[0 + 1][2], triData[3 + 1][2],
          /// triData[6 + 1][2], triData[9 + 1][2]);
          //////dataArray[7] = _mm_setr_ps(triData[0 + 2][2], triData[3 + 2][2],
          /// triData[6 + 2][2], triData[9 + 2][2]);

          uint16_t* a = pCompressVertices + pCompressIndices[0];
          uint16_t* b = pCompressVertices + pCompressIndices[3];
          uint16_t* c = pCompressVertices + pCompressIndices[6];
          uint16_t* d = pCompressVertices + pCompressIndices[9];

          // dataArray[0] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[0] = a[0];
          pData[1] = b[0];
          pData[2] = c[0];
          pData[3] = d[0];
          // dataArray[3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[12] = a[1];
          pData[13] = b[1];
          pData[14] = c[1];
          pData[15] = d[1];
          // dataArray[6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[24] = a[2];
          pData[25] = b[2];
          pData[26] = c[2];
          pData[27] = d[2];

          a = pCompressVertices + pCompressIndices[1];
          b = pCompressVertices + pCompressIndices[4];
          c = pCompressVertices + pCompressIndices[7];
          d = pCompressVertices + pCompressIndices[10];

          // dataArray[2] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[8] = a[0];
          pData[8 + 1] = b[0];
          pData[8 + 2] = c[0];
          pData[8 + 3] = d[0];
          // dataArray[2 + 3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[20] = a[1];
          pData[20 + 1] = b[1];
          pData[20 + 2] = c[1];
          pData[20 + 3] = d[1];
          // dataArray[2 + 6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[32] = a[2];
          pData[32 + 1] = b[2];
          pData[32 + 2] = c[2];
          pData[32 + 3] = d[2];

          a = pCompressVertices + pCompressIndices[2];
          b = pCompressVertices + pCompressIndices[5];
          c = pCompressVertices + pCompressIndices[8];
          d = pCompressVertices + pCompressIndices[11];

          // dataArray[1] = _mm_setr_ps(a[0], b[0], c[0], d[0]);
          pData[4] = a[0];
          pData[4 + 1] = b[0];
          pData[4 + 2] = c[0];
          pData[4 + 3] = d[0];
          // dataArray[1 + 3] = _mm_setr_ps(a[1], b[1], c[1], d[1]);
          pData[16] = a[1];
          pData[16 + 1] = b[1];
          pData[16 + 2] = c[1];
          pData[16 + 3] = d[1];
          // dataArray[1 + 6] = _mm_setr_ps(a[2], b[2], c[2], d[2]);
          pData[28] = a[2];
          pData[28 + 1] = b[2];
          pData[28 + 2] = c[2];
          pData[28 + 3] = d[2];

          pCompressIndices += 12;
        } else {
          __m128i I0XY = vertexData[0];
          __m128i I1XY = vertexData[2];
          __m128i I2XY = vertexData[1];

          // Vertex transformation - first W, then X & Y after camera plane
          // culling, then Z after backface culling
          __m128i Xi0 = _mm_srli_epi32(I0XY, 16);
          __m128i Xi1 = _mm_srli_epi32(I1XY, 16);
          __m128i Xi2 = _mm_srli_epi32(I2XY, 16);

          dataArray[0] = _mm_cvtepi32_ps(Xi0);
          dataArray[2] = _mm_cvtepi32_ps(Xi1);
          dataArray[1] = _mm_cvtepi32_ps(Xi2);

          __m128i mask = _mm_set1_epi32(65535);
          __m128i Yi0 = _mm_and_si128(I0XY, mask);
          __m128i Yi1 = _mm_and_si128(I1XY, mask);
          __m128i Yi2 = _mm_and_si128(I2XY, mask);
          dataArray[3] = _mm_cvtepi32_ps(Yi0);
          dataArray[5] = _mm_cvtepi32_ps(Yi1);
          dataArray[4] = _mm_cvtepi32_ps(Yi2);

          __m128i I12Z = vertexData[3];

          dataArray[6 + 1] = _mm_cvtepi32_ps(_mm_srli_epi32(I12Z, 16));
          dataArray[6 + 2] = _mm_cvtepi32_ps(_mm_and_si128(I12Z, mask));

          int odd = packetIdx & 1;
          int even = odd ^ 1;
          uint64_t* pV = (uint64_t*)vertexData;
          // odd  -> -1
          // even -> 8
          pV += (even << 3) - odd;
          __m128i extra =
              _mm_unpacklo_epi16(_mm_set_epi64x(0, pV[0]), _mm_setzero_si128());
          dataArray[6] = _mm_cvtepi32_ps(extra);
          vertexData += 4 ^ even;
        }

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { this->DebugData[kP4Total]++; });

        packetIdx++;

        __m128 mat30 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 0);
        __m128 mat31 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 1);
        __m128 mat32 = _mm_shuffle_ps_single_index(mOccluderCache.mat[3], 2);

        __m128 W[3];
        W[faceIdx0] = _mm_fmadd_ps(
            dataArray[0], mat30,
            _mm_fmadd_ps(dataArray[3], mat31,
                         _mm_fmadd_ps(dataArray[6], mat32, mat33)));
        W[1] = _mm_fmadd_ps(
            dataArray[2], mat30,
            _mm_fmadd_ps(dataArray[5], mat31,
                         _mm_fmadd_ps(dataArray[8], mat32, mat33)));
        W[faceIdx2] = _mm_fmadd_ps(
            dataArray[1], mat30,
            _mm_fmadd_ps(dataArray[4], mat31,
                         _mm_fmadd_ps(dataArray[7], mat32, mat33)));

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          this->DebugData[kPrimitiveTotalInput] += 4;
          this->DebugData[kP4NearClipInput] += (int)possiblyNearClipped << 2;
        });

        __m128 W0W1W2;
        if (possiblyNearClipped) {
          // All W < 0 means fully culled by camera plane
          W0W1W2 = _mm_and_ps(_mm_and_ps(W[0], W[1]), (W[2]));
          if (_mm_same_sign1_soc(W0W1W2)) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              this->DebugData[kP4CameraNearPlaneCull]++;
              this->DebugData[kPrimitiveCameraNearPlaneCull] += 4;
            });

            continue;
          }
        } else {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kPrimitiveValidNum] = 4; });
        }

        __m128 primitiveValid = _mm_set1_ps(-0.0f);
        if (possiblyNearClipped) {
          primitiveValid = _mm_xor_ps(W0W1W2, _mm_set1_ps(-0.0f));

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
            DebugData[kPrimitiveValidNum] =
                GetValidPrimitiveNum(primitiveValid);
            this->DebugData[kPrimitiveCameraNearPlaneCull] +=
                4 - DebugData[kPrimitiveValidNum];
          });
        }

        __m128 X[3], Y[3];

        __m128 mat00 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 0);
        __m128 mat01 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 1);
        __m128 mat02 = _mm_shuffle_ps_single_index(mOccluderCache.mat[0], 2);
        X[faceIdx0] = _mm_fmadd_ps(
            dataArray[0], mat00,
            _mm_fmadd_ps(dataArray[3], mat01,
                         _mm_fmadd_ps(dataArray[6], mat02, mat03)));
        X[1] = _mm_fmadd_ps(
            dataArray[2], mat00,
            _mm_fmadd_ps(dataArray[5], mat01,
                         _mm_fmadd_ps(dataArray[8], mat02, mat03)));
        X[faceIdx2] = _mm_fmadd_ps(
            dataArray[1], mat00,
            _mm_fmadd_ps(dataArray[4], mat01,
                         _mm_fmadd_ps(dataArray[7], mat02, mat03)));

        __m128 mat10 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 0);
        __m128 mat11 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 1);
        __m128 mat12 = _mm_shuffle_ps_single_index(mOccluderCache.mat[1], 2);
        Y[faceIdx0] = _mm_fmadd_ps(
            dataArray[0], mat10,
            _mm_fmadd_ps(dataArray[3], mat11,
                         _mm_fmadd_ps(dataArray[6], mat12, mat13)));
        Y[1] = _mm_fmadd_ps(
            dataArray[2], mat10,
            _mm_fmadd_ps(dataArray[5], mat11,
                         _mm_fmadd_ps(dataArray[8], mat12, mat13)));
        Y[faceIdx2] = _mm_fmadd_ps(
            dataArray[1], mat10,
            _mm_fmadd_ps(dataArray[4], mat11,
                         _mm_fmadd_ps(dataArray[7], mat12, mat13)));

        // Clamp W and invert
        __m128 invW[3];

        {
          // StressTest. Input junk value to make it does not crash
          // if (possiblyNearClipped)
          //{
          //	W[0] = _mm_set1_ps( 1.17549435082e-38);
          //	W[1] = _mm_set1_ps(1);
          //	W[2] = _mm_set1_ps(1);
          // }

          // this error might up to 1 pixel for x, and y
          invW[0] = _mm_rcp_ps(W[0]);
          invW[1] = _mm_rcp_ps(W[1]);
          invW[2] = _mm_rcp_ps(W[2]);
        }
        bool treatNearClip = possiblyNearClipped;
        if (possiblyNearClipped) {
          __m128 allInfront = _mm_min_ps(_mm_min_ps(W[0], W[1]), W[2]);
          allInfront = _mm_cmplt_ps(allInfront, _mm_set1_ps(0.00001f));

          if (!_mm_same_sign0(allInfront))  // near plane clipped
          {
            __m128 lowerBound = _mm_set1_ps(-maxInvW);
            __m128 upperBound = _mm_set1_ps(+maxInvW);

            invW[0] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[0]));
            invW[1] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[1]));
            invW[2] = _mm_min_ps(upperBound, _mm_max_ps(lowerBound, invW[2]));
            treatNearClip = true;
          }
        }

        X[0] = _mm_mul_ps(X[0], invW[0]);
        X[1] = _mm_mul_ps(X[1], invW[1]);
        X[2] = _mm_mul_ps(X[2], invW[2]);

        Y[0] = _mm_mul_ps(Y[0], invW[0]);
        Y[1] = _mm_mul_ps(Y[1], invW[1]);
        Y[2] = _mm_mul_ps(Y[2], invW[2]);

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          this->DebugData[kP4DrawTriangle]++;
          this->DebugData[kBlockPacketId] = packetIdx;
        });

        if (possiblyNearClipped) {
          if (treatNearClip) {
            DrawTriangle<true, bBackFaceCulling>(X, Y, invW, W, primitiveValid);
          } else {
            DrawTriangle<false, bBackFaceCulling>(X, Y, invW, W,
                                                  primitiveValid);
          }
        } else {
          DrawTriangle<false, bBackFaceCulling>(X, Y, invW, W, primitiveValid);
        }

      } while (packetIdx < triPacketCount);

      if (PrimitiveDataCompressed == true) {
        return;
      }
    } while (faceNum != 0);
  }
#pragma endregion TrisProcessing
}
