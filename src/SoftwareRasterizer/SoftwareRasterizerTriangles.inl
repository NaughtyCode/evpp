/*
 * Rasterizes a batch of up to four triangles. The template parameters remove
 * runtime branches for near-plane clipping and back-face culling, while the
 * implementation updates HiZ and per-block depth storage conservatively.
 */
template <bool possiblyNearClipped, bool bBackFaceCulling>
void Rasterizer::DrawTriangle(__m128* x, __m128* y, __m128* invW, __m128* W,
                              __m128 primitiveValid) {
  __m128 edgeNormalsX[3], edgeNormalsY[3];
  edgeNormalsX[0] = _mm_sub_ps(y[1], y[0]);
  edgeNormalsX[1] = _mm_sub_ps(y[2], y[1]);

  edgeNormalsY[0] = _mm_sub_ps(x[0], x[1]);
  edgeNormalsY[1] = _mm_sub_ps(x[1], x[2]);

  // Area and backface culling
  __m128 negativeArea = _mm_fmsub_ps(
      edgeNormalsX[1], edgeNormalsY[0],
      _mm_mul_ps(edgeNormalsX[0], edgeNormalsY[1]));  // negative negativeArea

  if (bBackFaceCulling == false) {
    __m128 swapMask;
    bool needRearrangeTriangle = false;
    // Need to flip back faceNum test for each W < 0
    if (possiblyNearClipped) {
      __m128i areaCheckMask = _mm_set1_epi32(0x7fffffff);

      // area threshold change to 0x7f000000
      // previous setting is 0x53800000 1.09951162778e+12
      __m128i areaThreshold = _mm_set1_epi32(0x7f000000);
      __m128i checkArea = _mm_and_si128(
          _mm_castps_si128(negativeArea),
          areaCheckMask);  // after this, valid Mask is positive Area
      __m128i validMask = _mm_cmplt_epi32(checkArea, areaThreshold);

      primitiveValid = _mm_and_ps(_mm_castsi128_ps(validMask), primitiveValid);

      // flip negativeArea sign if any W is negative
      __m128 FlipW = _mm_xor_ps(W[0], _mm_xor_ps(W[1], W[2]));
      FlipW = _mm_and_ps(FlipW, _mm_set1_ps(-0.0f));
      __m128 validArea = _mm_xor_ps(negativeArea, FlipW);

      swapMask = _mm_cmpge_ps(validArea, _mm_setzero_ps());
      __m128 validSwapMask = _mm_and_ps(
          swapMask, primitiveValid);  // if triangle behind camera, ignore
      needRearrangeTriangle = _mm_same_sign0(validSwapMask) == false;
    } else {
      swapMask = _mm_cmpge_ps(negativeArea, _mm_setzero_ps());
      primitiveValid = _mm_cmpneq_ps(negativeArea, _mm_setzero_ps());
      needRearrangeTriangle = _mm_same_sign0(swapMask) == false;
    }

    if (needRearrangeTriangle) {
      // time to re-order the vertex indices
      // mainly swap vertex 1 and vertex 2
      __m128 temp;

      temp = _mm_blendv_ps(x[1], x[2], swapMask);
      x[2] = _mm_blendv_ps(x[2], x[1], swapMask);
      x[1] = temp;

      temp = _mm_blendv_ps(y[1], y[2], swapMask);
      y[2] = _mm_blendv_ps(y[2], y[1], swapMask);
      y[1] = temp;

      temp = _mm_blendv_ps(W[1], W[2], swapMask);
      W[2] = _mm_blendv_ps(W[2], W[1], swapMask);
      W[1] = temp;

      temp = _mm_blendv_ps(invW[1], invW[2], swapMask);
      invW[2] = _mm_blendv_ps(invW[2], invW[1], swapMask);
      invW[1] = temp;

      edgeNormalsX[0] = _mm_sub_ps(y[1], y[0]);
      edgeNormalsX[1] = _mm_sub_ps(y[2], y[1]);

      edgeNormalsY[0] = _mm_sub_ps(x[0], x[1]);
      edgeNormalsY[1] = _mm_sub_ps(x[1], x[2]);

      swapMask = _mm_and_ps(swapMask, _mm_set1_ps(-0.0f));
      negativeArea = _mm_xor_ps(negativeArea, swapMask);
    }

  } else {
    // Need to flip back faceNum test for each W < 0
    if (possiblyNearClipped) {
      __m128i areaCheckMask = _mm_set1_epi32(0x7fffffff);

      // area threshold change to 0x7f000000
      // previous setting is 0x53800000 1.09951162778e+12
      __m128i areaThreshold = _mm_set1_epi32(0x7f000000);
      __m128i checkArea = _mm_and_si128(
          _mm_castps_si128(negativeArea),
          areaCheckMask);  // after this, valid Mask is positive Area
      __m128i validMask = _mm_cmplt_epi32(checkArea, areaThreshold);

      primitiveValid = _mm_and_ps(_mm_castsi128_ps(validMask), primitiveValid);

      // flip negativeArea sign if any W is negative
      __m128 validArea =
          _mm_xor_ps(_mm_xor_ps(negativeArea, W[1]), _mm_xor_ps(W[0], W[2]));
      primitiveValid = _mm_and_ps(validArea, primitiveValid);
    } else {
      // respect primitiveValid as it might come from Quad
      primitiveValid = _mm_and_ps(negativeArea, primitiveValid);
    }

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      int temp = GetValidPrimitiveNum(primitiveValid);
      this->DebugData[kPrimitiveBackfaceCull] +=
          DebugData[kPrimitiveValidNum] - temp;
      DebugData[kPrimitiveValidNum] = temp;
    });

    if (_mm_same_sign0(primitiveValid) == true) {
      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { this->DebugData[kP4BackfaceCull]++; });

      return;
    }
  }

  __m128 minFx, minFy, maxFx, maxFy;
  __m128 NearClipMaskMin;
  if (possiblyNearClipped) {
    // Clipless bounding box computation
    __m128 infP = _mm_set1_ps(+10000.0f);
    //__m128 infN = _mm_set1_ps(-10000.0f);

    // Find  interval of points with W > 0
    //__m128 zero = _mm_setzero_ps();
    __m128 infN = _mm_setzero_ps();
    __m128 maskMin0 = _mm_cmplt_ps(W[0], infN);
    __m128 maskMin1 = _mm_cmplt_ps(W[1], infN);
    __m128 maskMin2 = _mm_cmplt_ps(W[2], infN);
    NearClipMaskMin = _mm_or_ps(_mm_or_ps(maskMin0, maskMin1), maskMin2);

    // have some slight improvement
    __m128 minPx = _mm_min_ps(_mm_min_ps(_mm_blendv_ps(x[0], infP, maskMin0),
                                         _mm_blendv_ps(x[1], infP, maskMin1)),
                              _mm_blendv_ps(x[2], infP, maskMin2));

    __m128 minPy = _mm_min_ps(_mm_min_ps(_mm_blendv_ps(y[0], infP, maskMin0),
                                         _mm_blendv_ps(y[1], infP, maskMin1)),
                              _mm_blendv_ps(y[2], infP, maskMin2));

    __m128 maxPx = _mm_max_ps(_mm_max_ps(_mm_blendv_ps(x[0], infN, maskMin0),
                                         _mm_blendv_ps(x[1], infN, maskMin1)),
                              _mm_blendv_ps(x[2], infN, maskMin2));

    __m128 maxPy = _mm_max_ps(_mm_max_ps(_mm_blendv_ps(y[0], infN, maskMin0),
                                         _mm_blendv_ps(y[1], infN, maskMin1)),
                              _mm_blendv_ps(y[2], infN, maskMin2));

    __m128 minNx = _mm_min_ps(_mm_min_ps(_mm_blendv_ps(infP, x[0], maskMin0),
                                         _mm_blendv_ps(infP, x[1], maskMin1)),
                              _mm_blendv_ps(infP, x[2], maskMin2));

    __m128 minNy = _mm_min_ps(_mm_min_ps(_mm_blendv_ps(infP, y[0], maskMin0),
                                         _mm_blendv_ps(infP, y[1], maskMin1)),
                              _mm_blendv_ps(infP, y[2], maskMin2));

    __m128 maxNx = _mm_max_ps(
        _mm_max_ps(_mm_and_ps(x[0], maskMin0), _mm_and_ps(x[1], maskMin1)),
        _mm_and_ps(x[2], maskMin2));

    __m128 maxNy = _mm_max_ps(
        _mm_max_ps(_mm_and_ps(y[0], maskMin0), _mm_and_ps(y[1], maskMin1)),
        _mm_and_ps(y[2], maskMin2));

    __m128 incAx = _mm_and_ps(minPx, _mm_cmple_ps(maxNx, minPx));
    __m128 incAy = _mm_and_ps(minPy, _mm_cmple_ps(maxNy, minPy));
    __m128 incBx = _mm_blendv_ps(maxPx, infP, _mm_cmpgt_ps(maxPx, minNx));
    __m128 incBy = _mm_blendv_ps(maxPy, infP, _mm_cmpgt_ps(maxPy, minNy));

    minFx = _mm_min_ps(incAx, incBx);
    minFy = _mm_min_ps(incAy, incBy);
    maxFx = _mm_max_ps(incAx, incBx);
    maxFy = _mm_max_ps(incAy, incBy);
  } else {
    // Standard bounding box inclusion
    minFx = _mm_min_ps(_mm_min_ps(x[0], x[1]), x[2]);
    maxFx = _mm_max_ps(_mm_max_ps(x[0], x[1]), x[2]);

    minFy = _mm_min_ps(_mm_min_ps(y[0], y[1]), y[2]);

    maxFy = _mm_max_ps(_mm_max_ps(y[0], y[1]), y[2]);
  }

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
  uint32_t validMask = _mm_movemask_ps(primitiveValid);

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    int temp = GetValidPrimitiveNum(primitiveValid);
    this->DebugData[kPrimitiveFrustumCull] +=
        DebugData[kPrimitiveValidNum] - temp;
    DebugData[kPrimitiveValidNum] = temp;
  });

  if (validMask == 0) {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      this->DebugData[kP4FrustumCull]++;
      assert(DebugData[kPrimitiveValidNum] == 0);
    });

    return;
  }

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { this->DebugData[kP4PassFrustumCull]++; });

  // Compute Z from linear relation with 1/W
  __m128 maxZ = _mm_fmadd_ps(_mm_max_ps(_mm_max_ps(invW[0], invW[1]), invW[2]),
                             mOccluderCache.c1, mOccluderCache.c0);
  // If any W < 0, assume maxZ = 1 (effectively disabling Hi-Z)
  if (possiblyNearClipped) {
    //__m128 maskWSign = _mm_cmplt_ps(_mm_or_ps(_mm_or_ps(wSign[0], wSign[1]),
    //_mm_or_ps(wSign[2], wSign[0])), _mm_setzero_ps());
    //__m128 maskWSign = _mm_cmplt_ps(_mm_or_ps(_mm_or_ps(wSign[0], wSign[1]),
    // wSign[2]), _mm_setzero_ps());
    //__m128 MAX_depthv = _mm_castsi128_ps(_mm_set1_epi32(0x0ffff000));
    // maxZ = _mm_blendv_ps(maxZ, MAX_depthv, NearClipMaskMin); //

    // maxZ = _mm_min_ps(maxZ, MAX_depthv);
    maxZ = _mm_or_ps(maxZ, NearClipMaskMin);  // save one load and _mm_blendv_ps
  } else {
    // maxZ = _mm_min_ps(maxZ, _mm_castsi128_ps(_mm_set1_epi32(0x0ffff000)));
  }

  __m128i maxZi = PackPositiveBatchZ(maxZ);

  uint32_t* depthBounds = (uint32_t*)&maxZi;

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
        if (possiblyNearClipped && NEAR_CLIP_SURE_VISIBLE_OPTIMIZATION) {
          if (primitiveMaxZ == MAX_DEPTH) {
            alivePrimitive |= 1 << primitiveIdx;
            continue;
          }
        }

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

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { DebugData[kP4EarlyHizCullPass]++; });

  if (bPixelAABBClipping) {
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
    HandleDrawMode<3>(x, y, invW, validMask);
    if (bDebugOccluderOnly) {
      if (this->DebugData[kBlockPacketPrimitiveDebug] == 0) return;
      this->DebugData[kBlockPacketPrimitiveDebug] = 0;
    }
  }

  // delay the calculation of edgeNormal X Y, 2,3
  edgeNormalsX[2] = _mm_sub_ps(y[0], y[2]);
  edgeNormalsY[2] = _mm_sub_ps(x[2], x[0]);

  __m128 invArea;
  if (possiblyNearClipped) {
    // Do a precise division to reduce error in depth plane. Note that the
    // negativeArea computed here differs from the rasterized region if W < 0,
    // so it can be very small for large covered screen regions.
    // invArea = _mm_div_ps(_mm_set1_ps(1.0f), negativeArea);
    invArea = _mm_div_ps(mOccluderCache.NegativeC1, negativeArea);
  } else {
    // invArea = _mm_rcp_ps(negativeArea);
    invArea = _mm_rcp_ps_div(negativeArea);
    invArea = _mm_mul_ps(invArea, mOccluderCache.NegativeC1);
  }

  __m128 z0 = _mm_fmadd_ps(invW[0], mOccluderCache.c1, mOccluderCache.c0);
  __m128 z20 = _mm_sub_ps(invW[2], invW[0]);
  __m128 z12 = _mm_sub_ps(invW[1], invW[2]);

  // Compute screen space depth plane
  __m128 depthPlane[3];
  depthPlane[1] = _mm_mul_ps(
      invArea,
      _mm_fmsub_ps(z20, edgeNormalsX[1], _mm_mul_ps(z12, edgeNormalsX[2])));
  depthPlane[2] = _mm_mul_ps(
      invArea,
      _mm_fmsub_ps(z20, edgeNormalsY[1], _mm_mul_ps(z12, edgeNormalsY[2])));

  if (bDepthAtCenterOptimization == false) {
    // Depth at center of first pixel
    auto one16 = _mm_set1_ps(1.0f / 16.0f);  // load into register once
    __m128 refX = _mm_sub_ps(one16, x[0]);
    __m128 refY = _mm_sub_ps(one16, y[0]);
    depthPlane[0] = _mm_fmadd_ps(refX, depthPlane[1],
                                 _mm_fmadd_ps(refY, depthPlane[2], z0));
  } else {
    // Depth at center of first pixel. Optimization. Save One _mm_sub_ps X
    // horizontally
    // allow vertical half pixel error. This would save 1 _mm_sub_ps Y
    // vertically and avoid load 1/16 into memory
    depthPlane[0] = _mm_sub_ps(
        z0, _mm_fmadd_ps(x[0], depthPlane[1], _mm_mul_ps(y[0], depthPlane[2])));
  }

  // Flip edges if W < 0
  __m128 edgeFlipMask[3];
  if (possiblyNearClipped) {
    edgeFlipMask[0] = _mm_xor_ps(invW[0], invW[1]);
    edgeFlipMask[1] = _mm_xor_ps(invW[1], invW[2]);
    edgeFlipMask[2] = _mm_xor_ps(invW[0], invW[2]);

    __m128 minusZero = _mm_set1_ps(-0.0f);
    edgeFlipMask[0] = _mm_and_ps(edgeFlipMask[0], minusZero);
    edgeFlipMask[1] = _mm_and_ps(edgeFlipMask[1], minusZero);
    edgeFlipMask[2] = _mm_and_ps(edgeFlipMask[2], minusZero);

    edgeNormalsX[0] = _mm_xor_ps(edgeNormalsX[0], edgeFlipMask[0]);
    edgeNormalsY[0] = _mm_xor_ps(edgeNormalsY[0], edgeFlipMask[0]);

    edgeNormalsX[1] = _mm_xor_ps(edgeNormalsX[1], edgeFlipMask[1]);
    edgeNormalsY[1] = _mm_xor_ps(edgeNormalsY[1], edgeFlipMask[1]);

    edgeNormalsX[2] = _mm_xor_ps(edgeNormalsX[2], edgeFlipMask[2]);
    edgeNormalsY[2] = _mm_xor_ps(edgeNormalsY[2], edgeFlipMask[2]);
  }

  // Normalize edge equations for lookup

  __m128 invLen[3];
  NormalizeEdge(edgeNormalsX[0], edgeNormalsY[0], invLen[0]);
  NormalizeEdge(edgeNormalsX[1], edgeNormalsY[1], invLen[1]);
  NormalizeEdge(edgeNormalsX[2], edgeNormalsY[2], invLen[2]);

  // edgeOffsets is calculated so that (x0, y0) (x1, y1) fall on the line
  // edgeNormalsX * X  + edgeNormalsY * Y + edgeOffsets = 0;
  // substitute (x0, y0)
  //(y1-y0) * invLen * x0 + (x0-x1) * invLen  * y0 + edgeOffsets = 0
  //=> edgeOffsets =  (x1y0 - y1x0) * invLen

  __m128 edgeOffsets[3];
  // Important not to use FMA here to ensure identical results between
  // neighboring edges
  edgeOffsets[0] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[1], y[0]), _mm_mul_ps(y[1], x[0])), invLen[0]);
  edgeOffsets[1] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[2], y[1]), _mm_mul_ps(y[2], x[1])), invLen[1]);
  edgeOffsets[2] = _mm_mul_ps(
      _mm_sub_ps(_mm_mul_ps(x[0], y[2]), _mm_mul_ps(y[0], x[2])), invLen[2]);

  // Flip edge offsets as well
  if (possiblyNearClipped) {
    edgeOffsets[0] = _mm_xor_ps(edgeOffsets[0], edgeFlipMask[0]);
    edgeOffsets[1] = _mm_xor_ps(edgeOffsets[1], edgeFlipMask[1]);
    edgeOffsets[2] = _mm_xor_ps(edgeOffsets[2], edgeFlipMask[2]);
  }

  // Quantize slopes
  __m128i slopeLookups[4];
  slopeLookups[0] = QuantizeSlopeLookup(edgeNormalsX[0], edgeNormalsY[0]);
  slopeLookups[1] = QuantizeSlopeLookup(edgeNormalsX[1], edgeNormalsY[1]);
  slopeLookups[2] = QuantizeSlopeLookup(edgeNormalsX[2], edgeNormalsY[2]);
  __m128i mergedLookup = _mm_or_si128(
      _mm_or_si128(slopeLookups[0], slopeLookups[1]), slopeLookups[2]);
  // slopeLookups[3] = _mm_cmpgt_epi32(mergedLookup, _mm_set1_epi32(64 * 64 -
  // 1)); faster than _mm_cmpgt_epi32(mergedLookup, _mm_set1_epi32(64 * 64 -
  // 1));
  slopeLookups[3] = _mm_srli_epi32(mergedLookup, 12);  // 12 means 64*64

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    this->DebugData[kP4PassCull]++;
    mergedLookup = _mm_cmplt_epi32(mergedLookup, _mm_set1_epi32(64 * 64));
    int latestMask = _mm_movemask_ps(_mm_castsi128_ps(mergedLookup));
    int rejected = 0;
    uint32_t aliveMask = 0;
    uint32_t alivePrimitiveTemp = validMask;
    do {
      int j = (alivePrimitiveTemp & 3);
      aliveMask |= 1 << j;
      alivePrimitiveTemp >>= 2;
    } while (alivePrimitiveTemp > 0);

    rejected += ((aliveMask & 1) > 0) && ((latestMask & 1) == 0);
    rejected += ((aliveMask & 2) > 0) && ((latestMask & 2) == 0);
    rejected += ((aliveMask & 4) > 0) && ((latestMask & 4) == 0);
    rejected += ((aliveMask & 8) > 0) && ((latestMask & 8) == 0);

    DebugData[kPrimitiveValidNum] -= rejected;
    DebugData[kPrimitiveDegenerateCull] += rejected;

    DebugData[DebugData[kPrimitiveValidNum]]++;

    uint32_t mergedMask =
        aliveMask & _mm_movemask_ps(_mm_castsi128_ps(mergedLookup));
    if (mergedMask > 0) {
      this->DebugData[kP4Rasterized]++;
    }
  });

  do {
    uint32_t primitiveIdx = validMask & 3;
    validMask >>= 2;

    uint32_t* slopeLookup = ((uint32_t*)&slopeLookups) + primitiveIdx;
    if (slopeLookup[12] != 0) {
      continue;
    }

    if (bDebugOccluderOnly) {
      if (bDebugOccluderPixelX != -1 && bDebugOccluderPixelY != -1) {
        // if (this->DebugData[kBlockPacketId] != 13) return;
        if (primitiveIdx != this->DebugData[kBlockPacketPrimitive]) continue;
      }
    }

    const uint64_t* pRow0 = m_pMaskTable + slopeLookup[0];
    const uint64_t* pRow1 = m_pMaskTable + slopeLookup[4];
    const uint64_t* pRow2 = m_pMaskTable + slopeLookup[8];

    // Extract and prepare per-primitive dataprimitiveMaxZV
    uint32_t primitiveMaxZf = depthBounds[primitiveIdx];
    uint16_t primitiveMaxZ = (uint16_t)primitiveMaxZf;

    if (SupportDepthTill65K) {
      primitiveMaxZf |= 65536;
      primitiveMaxZf <<= 11;
    } else {
      primitiveMaxZf <<= 12;
    }

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
    static int DebugPrimitiveId = 0;
    if (bDumpTriangle && bDumpBlockColumnImage) {
      DebugPrimitiveId++;
      if (DebugPrimitiveId != 160) continue;
    }
#endif

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      if (slopeLookup[12] == 0)  // no degenerate case
      {
        this->DebugData[kPrimitiveRasterizedNum]++;
        DebugData[kPrimitiveValidNum]--;

        this->DebugData[kPrimitiveNearClipeRasterized] += possiblyNearClipped;
      }
    });

    float* depthPlaneData = ((float*)depthPlane) + primitiveIdx;

    float slope = depthPlaneData[8];

    __m128 depthBlockDelta = _mm_set1_ps(slope);

    __m128 depthRowDeltaBtm = _mm_setzero_ps();
    depthRowDeltaBtm =
        _mm_min_ps(_mm_set1_ps(slope * 0.375f), depthRowDeltaBtm);

    int xIncrease = (int)(depthPlaneData[4] > 0);
    int yIncrease = (int)(slope > 0);
    // data16: btmLeft 0 1 btmRight 2 3 topleft 4 5 topright 6 7
    // int	maxBlockIdx = (xIncrease << 1) | (yIncrease << 2);
    int maxBlockIdx = (xIncrease + yIncrease * 2) << 1;
    int minBlockIdx = 6 ^ maxBlockIdx;

    __m128 depthDx = _mm_set1_ps(depthPlaneData[4]);
    __m128 depthLeftBase;
    if (VRS_X4Y4_Optimzation) {
      // 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625,

      depthLeftBase = _mm_fmadd_ps(depthDx, xFactors[xIncrease],
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
    const uint32_t blockMaxX = boundData[4];
    const uint32_t blockMinY = boundData[8];
    const uint32_t blockMaxY = boundData[12];

    // Degenerate cases:
    // IMOC calculate horizontal line by regulate to pixel integer
    // IMOC apply ClipPolygon, and get the triangle area equals to zero
    // case: SuntempSlope.cap
    // status: partial fix for horizontal degenerate case which appear most
    // often only 2.5% primitive would pass this check. so impact is very low
    if (abs(slope) > 6E-32)  // floatCompressionBias = 2.5237386e-29f
    {
      if (blockMaxY == blockMinY &&
          (primitiveMaxZ > 205 * 256 && primitiveMaxZ != 65535)) {
        float* negativeAreaf = (float*)&negativeArea;
        if (abs(negativeAreaf[primitiveIdx]) <= 3) {
          bool flat = false;
          if (slopeLookup[0] == slopeLookup[4]) {
            flat = (slopeLookup[0] + slopeLookup[8]) == 4032;
          } else if (slopeLookup[0] == slopeLookup[8] ||
                     slopeLookup[8] == slopeLookup[4]) {
            flat = (slopeLookup[0] + slopeLookup[4]) == 4032;
          }
          if (flat) {
            __m128 minZ =
                _mm_fmadd_ps(_mm_min_ps(_mm_min_ps(invW[0], invW[1]), invW[2]),
                             mOccluderCache.c1, mOccluderCache.c0);
            __m128i minZi = _mm_castps_si128(minZ);
            int* minzp = (int*)&minZi;
            if (minzp[primitiveIdx] > 0) {
              // std::cout << "H reset maxz from " << primitiveMaxZ << " to " <<
              // (minzp[primitiveIdx] >> 12) << std::endl;
              primitiveMaxZf = minzp[primitiveIdx];
            }
            // else {
            //	__m128 maxZ = _mm_fmadd_ps(_mm_max_ps(_mm_max_ps(invW[0],
            // invW[1]), invW[2]), mOccluderCache.c1, mOccluderCache.c0);
            //	__m128i maxZi = _mm_castps_si128(maxZ);
            //	int * maxzp = (int*)&maxZi;
            //	primitiveMaxZf = maxzp[primitiveIdx];
            // }
          }
        }
      }
    }

    float* edgeNormalsXf = (float*)edgeNormalsX + primitiveIdx;
    float* edgeNormalsYf = (float*)edgeNormalsY + primitiveIdx;
    float* edgeOffsetsf = (float*)edgeOffsets + primitiveIdx;

    __m128 edgeNormalsXP =
        _mm_setr_ps(edgeNormalsXf[0], edgeNormalsXf[4], edgeNormalsXf[8], 0);
    __m128 edgeNormalsYP =
        _mm_setr_ps(edgeNormalsYf[0], edgeNormalsYf[4], edgeNormalsYf[8], 0);
    __m128 edgeOffsetsP =
        _mm_setr_ps(edgeOffsetsf[0], edgeOffsetsf[4], edgeOffsetsf[8], 0);

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
    const uint32_t blocksXRows = m_blocksXFullDataRows;

    uint32_t StartYBlocks = blocksX * blockMinY;

    uint16_t* pOffsetHiZ = m_pHiz + StartYBlocks;
    uint64_t* outblockRowData = m_pDepthBuffer + StartYBlocks * PairBlockNum;

    __m128 rowDepthLeftBtmOffset = _mm_add_ps(depthLeftBase, depthRowDeltaBtm);

    if (bPixelAABBClipping) {
      mPrimitiveBoundaryClip->UpdatePixelAABBData(primitiveIdx);
    }

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
        { DebugData[kBlockTotalPrimitives]++; });

    __m128 blockMinYf = _mm_set1_ps(float(blockMinY));
    __m128 rowDepthLeftOffsetY =
        _mm_fmadd_ps(depthBlockDelta, blockMinYf, rowDepthLeftBtmOffset);
    __m128 edgeOffsetY = _mm_fmadd_ps(edgeNormalY, blockMinYf, edgeOffset);

    __m128 offsetX =
        _mm_fmadd_ps(edgeNormalX, _mm_set1_ps((float)blockMinX), edgeOffsetY);
    int32_t PreviousSkip = 65536;

    uint32_t blockY = blockMinY;
    uint32_t NextBlockX = blockMinX;
    int32_t CurrentSkip = 0;

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      DebugData[kBlockTotal] +=
          (blockMaxX - blockMinX + 1) * (blockMaxY - blockMinY + 1);
    });

    while (true) {
      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { DebugData[kBlockDoWhileIfSave]++; });

      uint32_t ConvexOffset = 0;
      do {
        __m128i lookup = _mm_cvttps_epi32(offsetX);
        offsetX = _mm_add_ps(edgeNormalX, offsetX);
        lookup = _mm_max_epi32(lookup, _mm_setzero_si128());

        int32_t* lookIdx = (int32_t*)&lookup;
        int32_t idxOr = lookIdx[0] | lookIdx[1] | lookIdx[2];
        if (idxOr > 63) {
          NextBlockX++;

          if (bConvexOptimization) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              if (ConvexOffset > 0) {
                uint32_t blockX = NextBlockX - 1;
                DebugData[kBlockConvexRow10Cull] += blockMaxX - blockX;
              } else {
                DebugData[kBlockConvexRow10CullOverhead]++;
              }
            });

            // Convex Optimization 0: YesNo optimization. Stop if Block state
            // from see to not see
            NextBlockX |= ConvexOffset;
            CurrentSkip++;
          }

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockOneSureZeroCull]++; });

          continue;
        }

        uint32_t blockX = NextBlockX;
        NextBlockX++;

        // put the convex optimization here
        // because there are lots of single block update
        if (bConvexOptimization) {
          ConvexOffset = 65536;

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockConvexRow10CullOverhead]++; });
        }

        uint16_t* pBlockRowHiZ = pOffsetHiZ + blockX;
        if (pBlockRowHiZ[0] >= primitiveMaxZ) {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockPrimitiveMaxLessThanMinCull]++; });

          continue;
        }

        uint64_t blockMask = -1;  // in case of all 0, the whole block is active
        if (idxOr != 0) {
          blockMask = pRow0[lookIdx[0]];
          blockMask &= pRow1[lookIdx[1]];
          blockMask &= pRow2[lookIdx[2]];
          // No pixels covered => skip block
          if (blockMask == 0) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                { DebugData[kBlockMaskJointZeroCull]++; });

            continue;
          }
        }

        if (bDumpBlockColumnImage) {
          if (blockX == DebugDumpBlockX && blockY == DebugDumpBlockY) {
          } else
            continue;
        }

        // draw triangle
        if (VRS_X4Y4_Optimzation) {
          __m128i rowDepthLeft = _mm_castps_si128(_mm_fmadd_ps(
              depthDx, _mm_set1_ps((float)blockX), rowDepthLeftOffsetY));

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
                CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                  DebugData[kBlockRenderPartial]++;
                  DebugData[kBlockRenderTotal]++;
                });

                if (bPixelAABBClipping) {
                  blockMask &=
                      mPrimitiveBoundaryClip->GetPixelAABBMask(blockX, blockY);
                  if (blockMask == 0) {
                    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                        { DebugData[kBlockAabbClipToZero]++; });

                    continue;
                  }
                }

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
                this->mUpdateAnyBlock = true;

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
                    if (pBlockRowHiZ[0] == 0) {
                      DebugData[kBlockRenderInitial]++;
                      DebugData[kBlockRenderInitialFull]++;
                    }
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

                  pBlockRowHiZ[m_HizBufferSize] = maxBlockDepth;
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
              if (blockMask != -1 && bPixelAABBClipping) {
                blockMask &=
                    mPrimitiveBoundaryClip->GetPixelAABBMask(blockX, blockY);
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

          if (blockMask != -1 && bPixelAABBClipping) {
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
      } while (NextBlockX <= blockMaxX);
      if (blockY >= blockMaxY) {
        break;
      }
      blockY++;

      // CurrentSkip means the number of empty blocks before block covered by
      // triangle
      if (bConvexOptimization) {
        CurrentSkip -= NextBlockX >> 16;
        if (CurrentSkip <= PreviousSkip)  //>90% chance enter this...
        {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kBlockConvexEdge31CullRowCheckPass]++; });

          if (ConvexOffset != 0) {
            pOffsetHiZ += blocksX;
            outblockRowData += blocksXRows;
            edgeOffsetY = _mm_add_ps(edgeOffsetY, edgeNormalY);
            rowDepthLeftOffsetY =
                _mm_add_ps(rowDepthLeftOffsetY, depthBlockDelta);

            // Convex Optimization 3: Edge24 Block Backward Optimization
            static constexpr bool enableEdge24Skip = true;

            if (enableEdge24Skip == false) {
              PreviousSkip = CurrentSkip;
              NextBlockX = blockMinX;
              CurrentSkip = 0;
            } else {
              // safe to skip: 2 * current - previous - 1
              int nextSkip = (CurrentSkip << 1) - PreviousSkip - 1;
              PreviousSkip = CurrentSkip;
              // http://www-mdp.eng.cam.ac.uk/web/library/enginfo/mdp_micro/lecture4/lecture4-3-3.html
              CurrentSkip = nextSkip & ~(nextSkip >>
                                         31);  // ARM right shift sign extension

              CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                DebugData[kBlockConvexEdge24Cull] += CurrentSkip;
                DebugData[kBlockConvexEdge24Check]++;
              });

              NextBlockX = blockMinX + CurrentSkip;
            }
            offsetX = _mm_fmadd_ps(edgeNormalX, _mm_set1_ps((float)NextBlockX),
                                   edgeOffsetY);
          } else {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                { DebugData[kBlockConvexAllZeroRow]++; });

            // Convex Optimization 1: NoNoFastRowScan Empty Row Fast Check
            // Optimization This would happen when triangle clip with image
            // region boundary
            uint32_t currentY = blockY;

            __m128 edgeNormalXBlockMinX =
                _mm_mul_ps(edgeNormalX, _mm_set1_ps((float)blockMinX));

            __m128 offsetMin = _mm_add_ps(edgeNormalXBlockMinX, edgeOffsetY);
            __m128 offsetMax =
                _mm_sub_ps(offsetX, edgeNormalX);  // roll back one step

            offsetMin = _mm_min_ps(offsetMin, offsetMax);

            // if head & tail both >= 63, the whole row could be skipped
            // any offset >= 63, the whole row could be skipped.
            offsetMin = _mm_sub_ps(offsetMin, _mm_set1_ps(63));

            uint32_t controlY = 0;
            do {
              CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                  { DebugData[kBlockConvexRow00CullNextScanRows]++; });

              offsetMin = _mm_add_ps(offsetMin, edgeNormalY);
              __m128i cmp = _mm_srai_epi32(_mm_castps_si128(offsetMin),
                                           31);  // take sign only

              uint32_t* mask = (uint32_t*)&cmp;
              uint32_t skipMask = mask[0] & mask[1] & mask[2];

              currentY += (skipMask + 1);  // skipMask + 1 is equivalanet to
                                           // skipMask == 0  for the value 0, -1
              controlY = currentY | skipMask;

              CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                if (skipMask == 0) {
                  DebugData[kBlockConvexRow00Cull] += blockMaxX - blockMinX + 1;
                }
              });

            } while (controlY <= blockMaxY);
            if (currentY <= blockMaxY) {
              int dy = currentY - blockY + 1;
              blockY = currentY;
              pOffsetHiZ += blocksX * dy;
              outblockRowData += blocksXRows * dy;
              __m128 dyFv = _mm_set1_ps((float)dy);
              edgeOffsetY = _mm_fmadd_ps(edgeNormalY, dyFv, edgeOffsetY);
              offsetX = _mm_add_ps(edgeNormalXBlockMinX, edgeOffsetY);
              rowDepthLeftOffsetY =
                  _mm_fmadd_ps(depthBlockDelta, dyFv, rowDepthLeftOffsetY);

              NextBlockX = blockMinX;
              CurrentSkip = 0;
            } else {
              break;
            }
          }
        } else {
          // Convex Optimization 2: Edge31 Block Forward Optimization
          // safe to skip: 2 * current - previous - 1
          // BlockMin     -> BlockMin + 2 * current - previous - 1
          // CurrentSkip  -> 0
          // PreviousSkip -> PreviousSkip + 1 - CurrentSkip;
          PreviousSkip = PreviousSkip + 1 -
                         CurrentSkip;  // - (CurrentSkip - PreviousSkip - 1)

          blockMinX += (CurrentSkip - PreviousSkip);

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
            int skipCount =
                (CurrentSkip -
                 PreviousSkip);  // 2 * CurrentSkip - PreviousSkip - 1;
            if (skipCount > 0) {
              if (blockMinX > blockMaxX) {
                skipCount = blockMaxX - (blockMinX - skipCount) + 1;
              }
              DebugData[kBlockConvexEdge31Cull] +=
                  skipCount * (1 + blockMaxY - blockY);
            }
            DebugData[kBlockConvexEdge31CullRowCheck]++;
          });

          if (blockMinX <= blockMaxX)  // 99% here
          {
            pOffsetHiZ += blocksX;
            outblockRowData += blocksXRows;
            edgeOffsetY = _mm_add_ps(edgeOffsetY, edgeNormalY);
            offsetX = _mm_fmadd_ps(edgeNormalX, _mm_set1_ps((float)blockMinX),
                                   edgeOffsetY);
            rowDepthLeftOffsetY =
                _mm_add_ps(rowDepthLeftOffsetY, depthBlockDelta);
            CurrentSkip = 0;
            NextBlockX = blockMinX;
          } else {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
                { DebugData[kBlockConvexEdge31CullRowCheckPassExit]++; });

            break;
          }
        }
      } else {
        pOffsetHiZ += blocksX;
        outblockRowData += blocksXRows;
        edgeOffsetY =
            _mm_fmadd_ps(edgeNormalY, _mm_set1_ps((float)blockY), edgeOffset);
        offsetX = _mm_fmadd_ps(edgeNormalX, _mm_set1_ps((float)blockMinX),
                               edgeOffsetY);
        rowDepthLeftOffsetY = _mm_add_ps(rowDepthLeftOffsetY, depthBlockDelta);
        NextBlockX = blockMinX;
      }
    }
  } while (validMask > 0);

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    if (bDebugOccluderOnly == false) {
      assert(DebugData[kPrimitiveValidNum] == 0);
    }
  });
}

/*
 * Splits active quad lanes into one or two triangle batches. Small active-lane
 * sets can be merged into a single triangle patch; otherwise the function emits
 * the two triangles that cover each quad, preserving clipping and culling
 * flags.
 */
template <bool possiblyNearClipped, bool bBackFaceCulling>
void Rasterizer::SplitToTwoTriangles(__m128* X, __m128* Y, __m128* W,
                                     __m128* invW, __m128 primitiveValid,
                                     int validMask) {
  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    this->DebugData[kP4QuadSplit]++;
    DebugData[kPrimitiveValidNumQuad] = DebugData[kPrimitiveValidNum];
  });

  if (bQuadToTriangleMergeOp) {
    uint32_t pValidIdx = mAliveIdxMask[validMask];

    if (pValidIdx < 16)  // at most two
    {
      uint32_t pInvalidIdx =
          mAliveIdxMask[15 ^ validMask];  // get invalid idx...
                                          // mNonAliveIdxMask[validMask] =
                                          // mAliveIdxMask[15 ^ validMask];

      // One triangle patch would take care the quads patch with active number
      // <= 2.
      float* Xf = (float*)X;
      float* Yf = (float*)Y;
      float* Wf = (float*)W;
      float* invWf = (float*)invW;
      float* validf = (float*)&primitiveValid;

      do {
        // Move index and mask to next set bit
        uint32_t fromIdx = pValidIdx & 3;
        uint32_t updateIdx = pInvalidIdx & 3;

        Xf[updateIdx] = Xf[fromIdx];
        Xf[updateIdx | 4] = Xf[fromIdx | 8];
        Xf[updateIdx | 8] = Xf[fromIdx | 12];

        Yf[updateIdx] = Yf[fromIdx];
        Yf[updateIdx | 4] = Yf[fromIdx | 8];
        Yf[updateIdx | 8] = Yf[fromIdx | 12];

        Wf[updateIdx] = Wf[fromIdx];
        Wf[updateIdx | 4] = Wf[fromIdx | 8];
        Wf[updateIdx | 8] = Wf[fromIdx | 12];

        invWf[updateIdx] = invWf[fromIdx];
        invWf[updateIdx | 4] = invWf[fromIdx | 8];
        invWf[updateIdx | 8] = invWf[fromIdx | 12];

        validf[updateIdx] = -0.0f;

        pValidIdx >>= 2;
        pInvalidIdx >>= 2;
      } while (pValidIdx > 0);

      if (possiblyNearClipped == true) {
        primitiveValid = _mm_and_ps(
            primitiveValid, _mm_xor_ps(_mm_and_ps(_mm_and_ps(W[2], W[0]), W[1]),
                                       _mm_set1_ps(-0.0f)));
      }
      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X, Y, invW, W,
                                                          primitiveValid);

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { DebugData[kQuadToTriangleMerge]++; });

      return;
    }
  }

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { DebugData[kQuadToTriangleSplit]++; });

  if (possiblyNearClipped == false) {
    if (bBackFaceCulling == false) {
      // once back face culling off, data might be changed. so deep copy here
      __m128 X2[3], Y2[3], W2[3], invW2[3];
      X2[0] = X[0];
      X2[1] = X[2];
      X2[2] = X[3];
      Y2[0] = Y[0];
      Y2[1] = Y[2];
      Y2[2] = Y[3];
      W2[0] = W[0];
      W2[1] = W[2];
      W2[2] = W[3];
      invW2[0] = invW[0];
      invW2[1] = invW[2];
      invW2[2] = invW[3];
      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X2, Y2, invW2, W2,
                                                          primitiveValid);

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
        DebugData[kPrimitiveValidNum] = DebugData[kPrimitiveValidNumQuad];
      });

      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X, Y, invW, W,
                                                          primitiveValid);
    } else {
      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X, Y, invW, W,
                                                          primitiveValid);
      X[1] = X[0];
      Y[1] = Y[0];
      invW[1] = invW[0];
      W[1] = W[0];

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
        DebugData[kPrimitiveValidNum] = DebugData[kPrimitiveValidNumQuad];
      });

      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(
          X + 1, Y + 1, invW + 1, W + 1, primitiveValid);
    }
  } else {
    // direct back to triangle approach
    auto W02 = _mm_and_ps(W[2], W[0]);
    __m128 minusZero = _mm_set1_ps(-0.0f);
    if (bBackFaceCulling == false) {
      // All W < 0 means fully culled by camera plane
      __m128 primitiveValid0 = _mm_and_ps(
          primitiveValid, _mm_xor_ps(_mm_and_ps(W02, W[3]), minusZero));
      if (_mm_same_sign0(primitiveValid0) == false) {
        // once back face culling off, data might be changed. so deep copy here
        __m128 X2[3], Y2[3], W2[3], invW2[3];
        X2[0] = X[0];
        X2[1] = X[2];
        X2[2] = X[3];
        Y2[0] = Y[0];
        Y2[1] = Y[2];
        Y2[2] = Y[3];
        W2[0] = W[0];
        W2[1] = W[2];
        W2[2] = W[3];
        invW2[0] = invW[0];
        invW2[1] = invW[2];
        invW2[2] = invW[3];
        DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X2, Y2, invW2, W2,
                                                            primitiveValid0);
      }
    }

    // All W < 0 means fully culled by camera plane
    __m128 primitiveValid0 = _mm_and_ps(
        primitiveValid, _mm_xor_ps(_mm_and_ps(W02, W[1]), minusZero));
    if (_mm_same_sign0(primitiveValid0) == false) {
      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
        DebugData[kPrimitiveValidNum] = DebugData[kPrimitiveValidNumQuad];
      });

      DrawTriangle<possiblyNearClipped, bBackFaceCulling>(X, Y, invW, W,
                                                          primitiveValid0);
    }

    if (bBackFaceCulling == true) {
      // All W < 0 means fully culled by camera plane
      primitiveValid0 = _mm_and_ps(
          primitiveValid, _mm_xor_ps(_mm_and_ps(W02, W[3]), minusZero));
      if (_mm_same_sign0(primitiveValid0) == false) {
        X[1] = X[0];
        Y[1] = Y[0];
        invW[1] = invW[0];
        W[1] = W[0];

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          DebugData[kPrimitiveValidNum] = DebugData[kPrimitiveValidNumQuad];
        });

        DrawTriangle<possiblyNearClipped, bBackFaceCulling>(
            X + 1, Y + 1, invW + 1, W + 1, primitiveValid0);
      }
    }
  }
}

// draw point p, q here
