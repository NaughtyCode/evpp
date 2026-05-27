#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Stores one 8x8 block mask into a debug image column. Set bits receive the
 * supplied depth value and unset bits receive a mid-gray marker.
 */
static void StoreBlock(uint64_t t0, int blockIdx, uint8_t* target,
                       uint8_t value) {
  if (bDumpBlockColumnImage == false) return;
  int startX = blockIdx * 16;

  for (int by = 0; by < 8; by++) {
    int pixelIdx = startX + (by * 80);
    unsigned char* dest = target + pixelIdx;
    for (int bx = 0; bx < 8; bx++) {
      // mapping pixel (x, y) to 64 bit
      uint64_t check = (t0 >> (8 * bx + 7 - by)) & 1;
      if (check > 0) {
        dest[0] = std::max<uint8_t>(value, dest[0]);
      } else {
        dest[0] = std::max<uint8_t>(127, dest[0]);
      }
      dest++;
    }
  }
}
#endif

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Dumps intermediate block-mask state for a triangle column, together with the
 * current full depth image. This is a native debug-only aid for diagnosing
 * rasterization mask evolution.
 */
void Rasterizer::DumpColumnBlock(uint64_t t0, uint64_t t1, uint64_t t2,
                                 uint16_t maxDepth, uint64_t blockMask) {
  if (bDumpBlockColumnImage == false) return;
  static int mCurrentDebugMaskCount = 0;
  int maskDumpWith = 80;
  int maskDumpHeight = 16 * 50;  // store max 100 column data
  static std::vector<uint8_t> buffer;
  if (buffer.size() == 0) {
    buffer.resize(maskDumpWith *
                  maskDumpHeight);  // only save r,g channel and b = r&g
    memset(&buffer[0], 0, buffer.size());
  }
  uint8_t* target = &buffer[0];

  target += mCurrentDebugMaskCount * 16 * maskDumpWith;
  mCurrentDebugMaskCount++;
  if (mCurrentDebugMaskCount > maskDumpHeight / 8) {
    return;
  }

  StoreBlock(t0, 0, target, maxDepth >> 8);
  StoreBlock(t1, 1, target, maxDepth >> 8);
  StoreBlock(t2, 2, target, maxDepth >> 8);
  StoreBlock(t0 & t1 & t2, 3, target, maxDepth >> 8);
  StoreBlock(blockMask, 4, target, 255);

  target = &buffer[0];
  std::string str = std::to_string(mCurrentDebugMaskCount);
  while (str.length() < 3) str = "0" + str;
  const std::string filename =
      "./Output/triangleNumber_" + str + "_Mask.png";
  {
    DumpGrayImage(filename, target, maskDumpWith, maskDumpHeight);
  }

  std::vector<uint8_t> img(static_cast<std::size_t>(this->m_totalPixels) * 2u);
  const std::string filenameImage =
      "./Output/triangleNumber_" + str + "_Full.png";
  {
    ReadBackDepth(img.data(), kDumpFull);
    DumpGrayImage(filenameImage, img.data(), this->m_width, this->m_height);
  }
}
#endif

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Updates a full-resolution 8x8 depth block with interpolated depth rows and a
 * coverage mask. The function also refreshes the HiZ minimum for the block so
 * later queries can skip fully covered regions quickly.
 */
void Rasterizer::UpdateBlockWithMaxZ(__m128 rowDepthLeft, __m128 rowDepthRight,
                                     uint64_t blockMask, __m128 depthRowDelta,
                                     __m128i primitiveMaxZV, __m128i* out,
                                     uint16_t* pBlockRowHiZ) {
  if (VRS_X4Y4_Optimzation) {
    return;
  }

  if (blockMask != -1) {
    __m128i interleavedBlockMask = _mm_unpacklo_epi8_soc(blockMask);

    if (pBlockRowHiZ[0] == 0) {
      pBlockRowHiZ[0] = MIN_UPDATED_BLOCK_DEPTH;

      for (uint32_t i = 0; i < 7; ++i) {
        auto current =
            PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);

        __m128i rowMask = _mm_srai_epi16(interleavedBlockMask, 15);
        out[i] = _mm_and_si128(rowMask, current);

        rowDepthLeft = _mm_add_ps(rowDepthLeft, depthRowDelta);
        rowDepthRight = _mm_add_ps(rowDepthRight, depthRowDelta);
        interleavedBlockMask =
            _mm_add_epi16(interleavedBlockMask, interleavedBlockMask);
      }
      __m128i rowMask = _mm_srai_epi16(interleavedBlockMask, 15);

      auto current =
          PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);
      out[7] = _mm_and_si128(rowMask, current);

      return;  // init-partial update, hizMin is surely zero. No need to
               // calculate
    } else {
      for (uint32_t i = 0; i < 7; ++i) {
        auto current =
            PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);

        __m128i rowMask = _mm_srai_epi16(interleavedBlockMask, 15);
        out[i] = _mm_max_epu16(out[i], _mm_and_si128(rowMask, current));

        rowDepthLeft = _mm_add_ps(rowDepthLeft, depthRowDelta);
        rowDepthRight = _mm_add_ps(rowDepthRight, depthRowDelta);
        interleavedBlockMask =
            _mm_add_epi16(interleavedBlockMask, interleavedBlockMask);
      }
      __m128i rowMask = _mm_srai_epi16(interleavedBlockMask, 15);

      auto current =
          PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);
      out[7] = _mm_max_epu16(out[7], _mm_and_si128(rowMask, current));
    }
  } else {
    if (pBlockRowHiZ[0] == 0) {
      for (uint32_t i = 0; i < 7; ++i) {
        out[i] =
            PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);

        rowDepthLeft = _mm_add_ps(rowDepthLeft, depthRowDelta);
        rowDepthRight = _mm_add_ps(rowDepthRight, depthRowDelta);
      }

      out[7] =
          PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);

      pBlockRowHiZ[0] = _mm_min_epu16(_mm_min_epu16(
          out[0], out[7]));  // initial full block, check first & last row
      return;
    } else {
      ////keep original for reference
      for (uint32_t i = 0; i < 7; ++i) {
        auto current =
            PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);

        out[i] = _mm_max_epu16(out[i], current);

        rowDepthLeft = _mm_add_ps(rowDepthLeft, depthRowDelta);
        rowDepthRight = _mm_add_ps(rowDepthRight, depthRowDelta);
      }

      auto current =
          PackDepthPremultiplied(rowDepthLeft, rowDepthRight, primitiveMaxZV);
      out[7] = _mm_max_epu16(out[7], current);
    }
  }

  __m128i newMinZ0 = _mm_min_epu16(out[0], out[1]);
  __m128i newMinZ2 = _mm_min_epu16(out[2], out[3]);
  __m128i newMinZ4 = _mm_min_epu16(out[4], out[5]);
  __m128i newMinZ6 = _mm_min_epu16(out[6], out[7]);
  __m128i newMinZ = _mm_min_epu16(_mm_min_epu16(newMinZ0, newMinZ2),
                                  _mm_min_epu16(newMinZ4, newMinZ6));
  uint16_t result = _mm_min_epu16(newMinZ);
  pBlockRowHiZ[0] = std::max<uint16_t>(MIN_UPDATED_BLOCK_DEPTH, result);
}

#endif

/*
 * Updates a partially covered checkerboard block in the compact MSCB layout.
 * It merges new bottom/top row depths, maintains coverage masks, and writes
 * both HiZ min and max values used by conservative occludee tests.
 */
void Rasterizer::UpdateBlockMSCBPartial(uint32_t* depth32, uint64_t blockMask,
                                        __m128i* out, uint64_t* maskData,
                                        uint16_t* pBlockRowHiZ,
                                        uint16_t maxBlockDepth) {
  mUpdateAnyBlock = true;
  __m128i depthBottom =
      _mm_setr_epi32(depth32[0], depth32[0], depth32[1], depth32[1]);
  __m128i depthTop =
      _mm_setr_epi32(depth32[2], depth32[2], depth32[3], depth32[3]);

  if (pBlockRowHiZ[0] != 0) {
    __m128i interleavedBlockMask = _mm_unpacklo_epi8_soc(blockMask);

    out[0] = _mm_max_epu16(
        out[0],
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthBottom));
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[1] = _mm_max_epu16(
        out[1],
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthBottom));
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[2] = _mm_max_epu16(
        out[2],
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[3] = _mm_max_epu16(
        out[3],
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));

    if (PairBlockNum == CheckerBoardVizMaskApproach) {
      maskData[0] |= blockMask;
      if (maskData[0] == -1) {
        __m128i newMinZ0 = _mm_min_epu16(out[0], out[1]);
        __m128i newMinZ2 = _mm_min_epu16(out[2], out[3]);
        pBlockRowHiZ[0] = _mm_min_epu16(_mm_min_epu16(newMinZ0, newMinZ2));

        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kBlockMinCompute4]++; });
      }
    } else {
      __m128i newMinZ0 = _mm_min_epu16(out[0], out[1]);
      __m128i newMinZ2 = _mm_min_epu16(out[2], out[3]);
      pBlockRowHiZ[0] = _mm_min_epu16(_mm_min_epu16(newMinZ0, newMinZ2)) |
                        MIN_UPDATED_BLOCK_DEPTH;

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { DebugData[kBlockMinCompute4]++; });
    }
    uint16_t* pBlockRowHiZMax = pBlockRowHiZ + m_HizBufferSize;
    pBlockRowHiZMax[0] = std::max<uint16_t>(maxBlockDepth, pBlockRowHiZMax[0]);
  } else {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      DebugData[kBlockRenderInitial]++;
      DebugData[kBlockRenderInitialPartial]++;
    });

    if (PairBlockNum == CheckerBoardVizMaskApproach) {
      maskData[0] = blockMask;
    }

    pBlockRowHiZ[m_HizBufferSize] = maxBlockDepth;
    pBlockRowHiZ[0] = MIN_UPDATED_BLOCK_DEPTH;
    __m128i interleavedBlockMask;
    interleavedBlockMask = _mm_unpacklo_epi8_soc(blockMask);

    out[0] =
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthBottom);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[1] =
        _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthBottom);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[2] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 2);
    out[3] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
    return;  // init-partial update, hizMin is surely zero. No need to calculate
  }
}

#if defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Updates a full 8x8 block layout from two SIMD depth-row groups. This path is
 * active only for the full-block storage mode and keeps the HiZ minimum in sync
 * with the updated per-pixel depth rows.
 */
void Rasterizer::UpdateBlock(__m128i* depthRows, uint64_t blockMask,
                             __m128i* out, uint16_t* pBlockRowHiZ) {
  if (PairBlockNum != FullBlockApproach) {
    return;
  }
  this->mUpdateAnyBlock = true;
  {
    __m128i depthBottom = depthRows[0];
    __m128i depthTop = depthRows[1];
    if (blockMask != -1) {
      __m128i interleavedBlockMask = _mm_unpacklo_epi8_soc(blockMask);
      if (pBlockRowHiZ[0] == 0) {
        pBlockRowHiZ[0] = MIN_UPDATED_BLOCK_DEPTH;

        out[0] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                               depthBottom);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[1] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                               depthBottom);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[2] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                               depthBottom);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[3] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                               depthBottom);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[4] =
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[5] =
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[6] =
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[7] =
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop);
        return;  // init-partial update, hizMin is surely zero. No need to
                 // calculate
      } else {
        out[0] = _mm_max_epu16(
            out[0], _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                  depthBottom));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[1] = _mm_max_epu16(
            out[1], _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                  depthBottom));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[2] = _mm_max_epu16(
            out[2], _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                  depthBottom));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[3] = _mm_max_epu16(
            out[3], _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                  depthBottom));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[4] = _mm_max_epu16(
            out[4],
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[5] = _mm_max_epu16(
            out[5],
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[6] = _mm_max_epu16(
            out[6],
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));
        interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
        out[7] = _mm_max_epu16(
            out[7],
            _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15), depthTop));
      }
    } else {
      ////keep original for reference

      // All pixels covered => skip edge tests
      if (pBlockRowHiZ[0] == 0) {
        out[0] = depthBottom;
        out[1] = depthBottom;
        out[2] = depthBottom;
        out[3] = depthBottom;
        out[4] = depthTop;
        out[5] = depthTop;
        out[6] = depthTop;
        out[7] = depthTop;
        pBlockRowHiZ[0] = _mm_min_epu16(_mm_min_epu16(depthBottom, depthTop));
        return;
      } else {
        out[0] = _mm_max_epu16(out[0], depthBottom);
        out[1] = _mm_max_epu16(out[1], depthBottom);
        out[2] = _mm_max_epu16(out[2], depthBottom);
        out[3] = _mm_max_epu16(out[3], depthBottom);
        out[4] = _mm_max_epu16(out[4], depthTop);
        out[5] = _mm_max_epu16(out[5], depthTop);
        out[6] = _mm_max_epu16(out[6], depthTop);
        out[7] = _mm_max_epu16(out[7], depthTop);
      }
    }
    {
      __m128i newMinZ0 = _mm_min_epu16(out[0], out[1]);
      __m128i newMinZ2 = _mm_min_epu16(out[2], out[3]);
      __m128i newMinZ4 = _mm_min_epu16(out[4], out[5]);
      __m128i newMinZ6 = _mm_min_epu16(out[6], out[7]);
      __m128i newMinZ = _mm_min_epu16(_mm_min_epu16(newMinZ0, newMinZ2),
                                      _mm_min_epu16(newMinZ4, newMinZ6));
      uint16_t result = _mm_min_epu16(newMinZ);
      pBlockRowHiZ[0] = std::max<uint16_t>(MIN_UPDATED_BLOCK_DEPTH, result);
    }
  }
}
#endif
