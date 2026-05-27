#if defined(CULLING_ENGINE_NATIVE_DEBUG) && \
    defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
/*
 * Writes a 256x256 visualization of a 64-bit checkerboard mask. Each source bit
 * is expanded to a 32x32 tile so debug output can be inspected visually.
 */
static void SaveCheckBoard(uint64_t mask, uint8_t* target) {
  // 64 x 64
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      int yu = y >> 5;
      int xu = x >> 5;

      uint32_t bitIndex = 8 * yu + (7 - xu);
      int bit = (mask >> bitIndex) & 1;
      if (bit) {
        target[y * 256 + x] = 255;
      } else {
        target[y * 256 + x] = 0;
      }
    }
  }
}
#endif

static void WriteUnalignedU64(unsigned char* target, uint64_t value) {
  std::memcpy(target, &value, sizeof(value));
}

static void WriteMetadataU64(unsigned char* target, std::size_t index,
                             uint64_t value) {
  WriteUnalignedU64(target + static_cast<std::size_t>(index) * sizeof(uint64_t),
                    value);
}

static int DepthBlockIndex(const unsigned char* target,
                           const unsigned char* block) {
  return static_cast<int>((block - target) / sizeof(uint64_t));
}

/*
 * Reads the rasterizer depth state into an 8-bit image buffer. Depending on the
 * requested mode it can dump raw depth, HiZ data, block masks, checkerboard
 * patterns, and occludee overlay metadata appended after the pixel data.
 */
bool Rasterizer::ReadBackDepth(unsigned char* target,
                               CullingEngine::DumpImageMode mode) {
#if defined(CULLING_ENGINE_NATIVE_DEBUG) && \
    defined(CULLING_ENGINE_SUPPORT_ALL_FEATURES)
  static constexpr uint64_t blackPattern = 0x55AA55AA55AA55AA;
  static constexpr uint64_t whitePattern = 0xAA55AA55AA55AA55;
  static constexpr uint64_t oddColumn = 0xAAAAAAAAAAAAAAAA;
  static constexpr uint64_t evenColumn = 0x5555555555555555;

  static constexpr uint64_t oddBlack = (oddColumn & blackPattern);
  static constexpr uint64_t evenBlack = (evenColumn & blackPattern);
  static constexpr uint64_t evenWhite = (evenColumn & whitePattern);
  static constexpr uint64_t oddWhite = (oddColumn & whitePattern);
  if (mode == kCheckerboardBlackPattern) {
    SaveCheckBoard(blackPattern, target);
    return true;
  }
  if (mode == kCheckerboardWhitePattern) {
    SaveCheckBoard(whitePattern, target);
    return true;
  }
  if (mode == kCheckerboardOddColumn) {
    SaveCheckBoard(oddColumn, target);
    return true;
  }
  if (mode == kCheckerboardEvenColumn) {
    SaveCheckBoard(evenColumn, target);
    return true;
  }
  if (mode == kCheckerboardOddBlack) {
    SaveCheckBoard(oddBlack, target);
    return true;
  }
  if (mode == kCheckerboardEvenBlack) {
    SaveCheckBoard(evenBlack, target);
    return true;
  }

  if (mode == kCheckerboardEvenWhite) {
    SaveCheckBoard(evenWhite, target);
    return true;
  }

  if (mode == kCheckerboardOddWhite) {
    SaveCheckBoard(oddWhite, target);
    return true;
  }

  if (mode == kDumpBlockMask) {
    memset(target, 0, sizeof(unsigned char) * 512 * 1024);
    int numofMinusOne = 0;
    uint64_t minusOne = -1;

    int zeroCount = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        uint64_t mask = 0;
        mask = m_precomputedRasterTables[y * 64 + x];

        if (mask == minusOne) {
          numofMinusOne++;
        }
        if (mask == 0) {
          zeroCount++;
        }
        int blockMin = x * 8 + y * 16 * 512;
        for (int by = 0; by < 8; by++) {
          int pixelIdx = blockMin + (by * 512);
          unsigned char* dest = target + pixelIdx;
          for (int bx = 0; bx < 8; bx++) {
            int maskIdx = (8 * bx + 7 - by);
            *dest = ((mask >> maskIdx) & 1) << 7;
            dest++;
          }
        }
      }
    }
    // it has been found that for case x <= 1, the mask value is -1. Fully
    // covered
    return true;
  }
#endif

#if defined(CULLING_ENGINE_NATIVE_DEBUG)

  if (mode == CullingEngine::DumpImageMode::kDumpHiz) {
    uint16_t* hzPointer = this->m_pHiz;

    uint8_t* targetImage = (uint8_t*)target;
    for (uint32_t blockY = 0; blockY < m_blocksY; ++blockY) {
      for (uint32_t blockX = 0; blockX < m_blocksX; ++blockX) {
        int index = blockY * m_blocksX + blockX;
        uint16_t zValue = hzPointer[index];
        targetImage[index] = zValue >> 8;
      }
    }
    return true;
  }
#endif

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    CULLING_ENGINE_LOG_DEBUG(
        "Readback depth state: blocks=%dx%d resolution=%dx%d blockSize=%d "
        "interleave=%d hizBufferSize=%d occluders=%d",
        m_blocksY, m_blocksX, m_width, m_height, m_blockSize,
        mInterleave.CurrentFrameInterleaveDrawing, m_HizBufferSize,
        mCurrValidOccluderNum);
  });

  // bool quickDumpForWindows = true;
  // if (CullingEngine::IS_ARM_PLATFORM || quickDumpForWindows)
  {
    if (mCurrValidOccluderNum == 0) {
      memset(target, 0, m_totalPixels * sizeof(uint8_t));
    } else if (this->mDebugRenderType >= RenderType::kRenderLine) {
      // copy point line data
      if (m_depthBufferPointLines.size() == m_totalPixels &&
          this->mInterleave.CurrentFrameInterleaveDrawing == false) {
        __m128i* extraRoot = (__m128i*)&this->m_depthBufferPointLines[0];
        int totalBlocks = (int)(this->m_totalPixels >> 3);
        for (int idx = 0; idx < totalBlocks; idx++) {
          WriteUnalignedU64(
              target + static_cast<std::size_t>(idx) * sizeof(uint64_t),
              _mm_getUint16Max8_soc(extraRoot[idx]));
        }
      }
    } else {
      bool mergeBuffer = mDebugRenderType == RenderType::kRenderMeshLine ||
                         mDebugRenderType == RenderType::kRenderMeshPoint;
      __m128i TargetBlockData[8];

      __m128i* extraRoot = nullptr;
      if (mergeBuffer) {
        extraRoot = (__m128i*)&this->m_depthBufferPointLines[0];
      }
      for (uint32_t blockY = 0; blockY < m_blocksY; ++blockY) {
        int base = blockY * m_blocksX;
        for (uint32_t blockX = 0; blockX < m_blocksX; ++blockX) {
          uint8_t* dest =
              (uint8_t*)target + (8 * blockX + m_width * (8 * blockY));
          int hizIdx = blockX + base;
          uint64_t* blockPtr = (m_pDepthBuffer + PairBlockNum * hizIdx);
          uint16_t hiz = this->m_pHiz[hizIdx];
          if (hiz == 0) {
            for (uint32_t y = 0; y < 8; ++y, dest += m_width) {
              if (mergeBuffer) {
                int count = DepthBlockIndex(target, dest);

                __m128i valid =
                    _mm_cmplt_epu16_soc(_mm_setzero_si128(), extraRoot[count]);
                WriteUnalignedU64(dest, _mm_getUint16Max8_soc(valid));
              } else {
                WriteUnalignedU64(dest, 0);
              }
            }
            continue;
          } else if (PairBlockNum == FullBlockApproach) {
            __m128i* fullBlock = (__m128i*)blockPtr;
            for (uint32_t y = 0; y < 8; ++y, dest += m_width) {
              __m128i source128 = fullBlock[y];
              WriteUnalignedU64(dest, _mm_getUint16Max8_soc(source128));
            }
            continue;
          }

          if (PairBlockNum <= PureCheckerBoardApproach + 1) {
            if (hiz <= MIN_UPDATED_BLOCK_DEPTH2) {
              uint64_t* maskData = GetMaskData(blockPtr, blockX & 1);
              __m128i* depthData = GetDepthData(blockPtr, (blockX & 1));

              RecoverPartialBlockData(TargetBlockData, depthData, maskData);
              __m128i* fullBlock = TargetBlockData;
              for (uint32_t y = 0; y < 8; ++y, dest += m_width) {
                __m128i blockData = fullBlock[y];
                if (mergeBuffer) {
                  int count = DepthBlockIndex(target, dest);
                  blockData =
                      CompareWithExtraBuffer(blockData, extraRoot[count]);
                }
                WriteUnalignedU64(dest, _mm_getUint16Max8_soc(blockData));
              }
            } else {
              __m128i* fullBlock = GetDepthData(blockPtr, (blockX & 1));

              if (bDumpCheckerboardImage) {
                for (int i = 0; i < 8; i++) {
                  TargetBlockData[i] = fullBlock[i >> 1];
                }
                GrayCheckBoardWhitePixel(TargetBlockData);
                for (uint32_t y = 0; y < 8; ++y, dest += m_width) {
                  __m128i blockData = TargetBlockData[y];
                  if (mergeBuffer) {
                    int count = DepthBlockIndex(target, dest);
                    blockData =
                        CompareWithExtraBuffer(blockData, extraRoot[count]);
                  }
                  WriteUnalignedU64(dest, _mm_getUint16Max8_soc(blockData));
                }
              } else {
                for (uint32_t y = 0; y < 8; ++y, dest += m_width) {
                  __m128i blockData = fullBlock[y >> 1];

                  if (SupportDepthTill65K) {
                    blockData = _mm_srli_epi32(blockData, 1);
                    blockData =
                        _mm_or_si128(blockData, _mm_set1_epi32(0x80008000));
                  }

                  if (mergeBuffer) {
                    int count = DepthBlockIndex(target, dest);
                    blockData =
                        CompareWithExtraBuffer(blockData, extraRoot[count]);
                  }
                  WriteUnalignedU64(dest, _mm_getUint16Max8_soc(blockData));
                }
              }
            }
          }
        }
      }
    }

    const std::size_t totalPixel =
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    const std::size_t currentSize = mOccludeeResults.size();
    unsigned char* imgMetaData = target + totalPixel;
    if (ShowOccludeeInDepthMap == false ||
        totalPixel / 8 <= mOccludeeResults.size() ||
        currentSize == 0)  // too many queries
    {
      WriteMetadataU64(imgMetaData, 0, 0);
    } else {
      WriteMetadataU64(imgMetaData, 0, static_cast<uint64_t>(currentSize));
      // sort according to depth value
      mOccludeeResults.reserve(currentSize + (currentSize >> 1));
      for (std::size_t idx = 1; idx < currentSize; idx += 2) {
        uint64_t merge = mOccludeeResults[idx] | ((uint64_t)idx << 8);
        mOccludeeResults.push_back(merge);
      }
      std::sort(mOccludeeResults.begin() + currentSize,
                mOccludeeResults.end());  // ascending order
      std::size_t metaDataIdx = 1;
      for (std::size_t idx = currentSize; idx < mOccludeeResults.size();
           idx++) {
        std::size_t currentIdx = static_cast<std::size_t>(
            (mOccludeeResults[idx] >> 8) & 0xFFFFFFFFu);
        WriteMetadataU64(imgMetaData, metaDataIdx++,
                         mOccludeeResults[currentIdx - 1]);
        WriteMetadataU64(imgMetaData, metaDataIdx++,
                         mOccludeeResults[currentIdx]);
      }
    }

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
    // debug data
    if (ShowOccludeeInDepthMap && PrintOccludeeState &&
        mOccludeeResults.size() > 0) {
      std::string developerGoldData(
          CullingEngineFrameCapture::Singleton.OutputDir);

      std::string file_name = developerGoldData + "/" +
                              std::to_string(this->m_width) + "_" +
                              std::to_string(this->m_height) + ".log";
      CULLING_ENGINE_LOG_DEBUG("Occludee state log path: %s",
                               file_name.c_str());
      FILE* mFileWriter = fopen(file_name.c_str(), "w");
      if (mFileWriter == nullptr) {
        CULLING_ENGINE_LOG_WARNING("Failed to open occludee state log: %s",
                                   file_name.c_str());
      } else {
        for (std::size_t idx = 0; idx < currentSize; idx += 2) {
          uint64_t value = mOccludeeResults[idx];
          bool visible = mOccludeeResults[idx | 1] & 1;

          uint16_t minX = value >> 48;

          uint16_t maxX = (value >> 32) & 0xFFFF;
          uint16_t minY = (value >> 16) & 0xFFFF;
          uint16_t maxY = value & 0xFFFF;
          // going to draw this rectangle

          fprintf(mFileWriter,
                  "OccludeeIdx %zu minx %d %d %d %d Visible %d depth %d\n ",
                  idx, minX, maxX, minY, maxY, (int)visible,
                  (int)((mOccludeeResults[idx | 1] >> 48)));
        }

        fclose(mFileWriter);
      }
    }
#endif
  }
  return true;
}

////////not used at the moment
/*
 * Reconstructs an approximate floating-point depth from the 16-bit compressed
 * depth representation. This helper is retained for diagnostics and precision
 * analysis of the compression format.
 */
float DecompressFloat(uint16_t depth) {
  const float bias = 3.9623753e+28f;  // 1.0f / floatCompressionBias

  union {
    uint32_t u;
    float f;
  } U = {uint32_t(depth) << 12};
  return U.f * bias;
}

/*
 * Normalizes an edge normal using a reciprocal Manhattan length. The rasterizer
 * uses this cheaper normalization for lookup-table slope quantization.
 */
static void NormalizeEdge(__m128& nx, __m128& ny, __m128& invLen) {
  invLen = _mm_rcp_ps(_mm_add_ps(_mm_abs_ps_soc(nx), _mm_abs_ps_soc(ny)));
  nx = _mm_mul_ps(nx, invLen);
  ny = _mm_mul_ps(ny, invLen);
}

/*
 * Maps a normalized edge direction to a compact slope lookup-table key. The X
 * component provides the major slope bucket and the sign of Y selects the
 * mirrored half of the table.
 */
static __m128i QuantizeSlopeLookup(__m128 nx, __m128 ny) {
  __m128i yNeg2 = _mm_castps_si128(ny);
  // auto yNegI2 = _mm_slli_epi32(_mm_srli_epi32(yNeg2, 31),
  // OFFSET_QUANTIZATION_BITS);
  auto yNegI2 = _mm_srli_epi32(yNeg2, 31 - OFFSET_QUANTIZATION_BITS);

  ////// Remap [-1, 1] to [0, SLOPE_QUANTIZATION / 2]
  // constexpr float mul = (SLOPE_QUANTIZATION_FACTOR / 2 - 1) * 0.5f;  //15.5
  // constexpr float add = mul + 0.5f;                                  //16
  //__m128i quantizedSlope = _mm_cvttps_epi32(_mm_fmadd_ps_soc(nx, mul,
  //_mm_set1_ps(add))); 10 angle degree for horizontal x sin(86.25 degree) /
  // (sin(86.25 degree) + cos(86.25 degree))= 0.93848823149
  //* (15.99) + 16 = 31.0111192627
  // sin(88.25 degree) / (sin(88.25 degree) + cos(88.25 degree)) 0.97035303345
  //  15.5+16 = 31
  // sin(4.25 degree) / (sin(4.25 degree) + cos(4.25 degree)) 0.06917243674
  // 0.06917243674 * (15.5) + 16 = 17.072
  //__m128i quantizedSlope2 = _mm_cvttps_epi32(_mm_fmadd_ps_soc(nx, 15.5f,
  //_mm_set1_ps(16.0f)));

  // 16.0f / 15.5f = 1.03225806452
  // 5.87747175411e-39/5.6938007618e-39  = 1.03225806452
  // 2.93873587706e-39/2.8469003809e-39
  //__m128i quantizedSlope =
  //_mm_castps_si128(_mm_fmadd_ps_soc(nx, 2.8469003809e-39,
  //_mm_set1_ps(2.93873587706e-39))); quantizedSlope =
  // _mm_srli_epi32(quantizedSlope, 17);
  //	auto q2 = _mm_slli_epi32(quantizedSlope, OFFSET_QUANTIZATION_BITS + 1);
  //   return _mm_or_si128(q2, yNegI2);

  if (ARMV7) {
    ////// Remap [-1, 1] to [0, SLOPE_QUANTIZATION / 2]
    constexpr float mul =
        (SLOPE_QUANTIZATION_FACTOR / 2 - 1) * 0.5f * 128;  // 15.5
    constexpr float add = mul + 0.5f * 128;                // 16
    __m128i quantizedSlope =
        _mm_cvttps_epi32(_mm_fmadd_ps_soc(nx, mul, _mm_set1_ps(add)));
    quantizedSlope = _mm_srli_epi32(quantizedSlope, 7);
    quantizedSlope = _mm_slli_epi32(quantizedSlope, 7);
    return _mm_and_si128(_mm_or_si128(quantizedSlope, yNegI2),
                         _mm_set1_epi32(4032));
  } else {
    // 16.0f / 15.5f = 1.03225806452
    // 5.87747175411e-39/5.6938007618e-39  = 1.03225806452
    // 2.93873587706e-39/2.8469003809e-39
    __m128i quantizedSlope = _mm_castps_si128(_mm_fmadd_ps_soc(
        nx, 3.55862547612e-40, _mm_set1_ps(3.67341984632e-40)));
    quantizedSlope = _mm_srli_epi32(quantizedSlope, 14);
    auto q2 = _mm_slli_epi32(quantizedSlope, OFFSET_QUANTIZATION_BITS + 1);
    return _mm_and_si128(_mm_or_si128(q2, yNegI2), _mm_set1_epi32(4032));
  }
}

/*
 * Combines edge normalization and slope quantization for rasterization setup.
 * The normalized components and reciprocal length are returned because later
 * edge setup code reuses all three values.
 */
static void NormalizeEdgeAndQuantizeSlope(__m128& nx, __m128& ny,
                                          __m128& invLen, __m128i& slope) {
  invLen = _mm_rcp_ps(_mm_add_ps(_mm_abs_ps_soc(nx), _mm_abs_ps_soc(ny)));
  nx = _mm_mul_ps(nx, invLen);
  ny = _mm_mul_ps(ny, invLen);
  slope = QuantizeSlopeLookup(nx, ny);
}

/*
 * Converts a sub-pixel edge offset to the precomputed mask-table offset bucket.
 * The input range is clamped to the finite table domain.
 */
static uint32_t QuantizeOffsetLookup(float offset) {
  float lookup = offset * OFFSET_mul + OFFSET_add;
  return std::min(std::max(int32_t(lookup), 0), OFFSET_QUANTIZATION_FACTOR - 1);
}
