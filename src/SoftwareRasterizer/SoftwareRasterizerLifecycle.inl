
/*
 * Releases per-instance caches and clears the shared mask-table cache when this
 * rasterizer owns the cached table. The mutex protects the process-wide cache
 * pointer shared by other Rasterizer instances.
 */
Rasterizer::~Rasterizer() {
  if (mPrimitiveBoundaryClip != nullptr) {
    delete mPrimitiveBoundaryClip;
  }
  delete[] DebugData;

  std::lock_guard<std::mutex> lock(g_i_mutex);
  if (MaskTableCache != nullptr && !m_precomputedRasterTables.empty()) {
    if (MaskTableCache == m_precomputedRasterTables.data()) {
      MaskTableCache = nullptr;
      // CULLING_ENGINE_LOG_DEBUG("Clear mask cache");
    }
  }
}

/*
 * Configures framebuffer-dependent storage after engine creation or resize.
 * This computes block dimensions, allocates depth and HiZ buffers, reuses or
 * builds the shared rasterization mask table, and updates coordinate clamps.
 */
void Rasterizer::SetResolution(unsigned int width, unsigned int height) {
  m_width = width;
  m_height = height;

  this->m_totalPixels = m_width * m_height;
  m_MaxCoord_WHWH =
      _mm_setr_epi32(m_width - 1, m_height - 1, m_width - 1, m_height - 1);

  mWidthIn1024 = width <= 1024;
  m_blocksX = width >> 3;
  m_blocksY = height >> 3;
  m_blocksYMinusOne = m_blocksY - 1;

  if (bOccludeeBitScanOp) mAnyDataBlockMask.resize(m_blocksY);

  mBlockWidthMin = 0;
  mBlockWidthMax = m_blocksX - 1;

  m_blocksXFullDataRows = m_blocksX * PairBlockNum;

  const std::size_t depthBufferWords =
      static_cast<std::size_t>(m_blocksY) *
      static_cast<std::size_t>(m_blocksXFullDataRows);
  m_depthBuffer.resize((depthBufferWords + 1u) / 2u);
  m_pDepthBuffer = reinterpret_cast<uint64_t*>(m_depthBuffer.data());

  this->m_blockSize = m_blocksX * m_blocksY;
  // 309 - 311 -> Align data to make the size can be divided by 8
  // All test cases still pass after comment of 309 - 311 so far. But we leave
  // it for safety.
  this->m_HizBufferSize = m_blockSize;

  int hizSize = m_HizBufferSize * 2;  // append hizMin, hizMax
  int counter64 = hizSize / 4;
  int MaskTable = (OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR);
  int totalMaskTable = MaskTable + counter64;

  int hizStart = MaskTable;

  if (m_precomputedRasterTables.size() == 0) {
    m_precomputedRasterTables.resize(totalMaskTable, 0);

    std::lock_guard<std::mutex> lock(g_i_mutex);
    if (MaskTableCache != nullptr) {
      // CULLING_ENGINE_LOG_DEBUG("Clone existing mask table cache");
      memcpy(m_precomputedRasterTables.data(), MaskTableCache,
             64 * 64 * sizeof(uint64_t));
    } else {
      PrecomputeRasterizationTable();
      MaskTableCache = m_precomputedRasterTables.data();
    }
  } else {
    if (m_precomputedRasterTables.size() < totalMaskTable) {
      std::lock_guard<std::mutex> lock(g_i_mutex);
      const bool ownsMaskTableCache =
          MaskTableCache == m_precomputedRasterTables.data();
      m_precomputedRasterTables.resize(totalMaskTable);
      if (ownsMaskTableCache) {
        MaskTableCache = m_precomputedRasterTables.data();
      }
    }
  }

  m_pMaskTable = m_precomputedRasterTables.data();
  m_pHiz = (uint16_t*)(m_pMaskTable + hizStart);
  m_pHizMax = m_pHiz + m_HizBufferSize;
  ConfigCoherent();

  if (mDebugRenderType != 0) {
    m_depthBufferPointLines.resize(this->m_totalPixels);
  }

  m_MaxCoordOccludee_WHWH =
      _mm_setr_epi32(m_width - 1, m_height - 1, m_width - 1, m_height - 1);
}

/*
 * Extracts clip-space frustum planes from the currently bound local-to-clip
 * matrix. The planes are used only for near-clipped bounds, where screen-space
 * projection alone is not enough to decide visibility safely.
 */
void Rasterizer::UpdateFrustumCullPlane() {
  if (bFrustumCullIfClip) {
    const __m128 mat0 = pLocalToClipRow[0];
    const __m128 mat1 = pLocalToClipRow[1];
    const __m128 mat2 = pLocalToClipRow[2];
    const __m128 mat3 = pLocalToClipRow[3];
    ////////Refer to
    /// http://www.lighthouse3d.com/tutorials/view-frustum-culling/clip-space-approach-extracting-the-planes/
    ////////Left Plane : x' = -1
    ////////Right Plane : x' = 1
    ////////Top Plane : y' = 1
    ////////Bottom Plane : y' = -1
    ////////Near Plane : z' = -1
    ////////Far Plane : z' = 1
    ////// Store rows
    m_FrustumPlane[0] = _mm_add_ps(mat3, mat0);  // left
    m_FrustumPlane[1] = _mm_sub_ps(mat3, mat0);  // right
    m_FrustumPlane[2] = _mm_add_ps(mat3, mat1);  // bottom
    m_FrustumPlane[3] = _mm_sub_ps(mat3, mat1);  // up
    m_FrustumPlane[4] = _mm_add_ps(mat3, mat2);  // near
    m_FrustumPlane[5] = _mm_sub_ps(mat3, mat2);  // far
  }
}
/*
 * Binds the matrix used for either occluder rasterization or occludee queries.
 * The method folds viewport and depth-bias transforms into columns that are
 * consumed by SIMD projection code, then stores them transposed for lane use.
 */
void Rasterizer::SetModelViewProjectionT(
    const CullingEngine::Matrix4x4& localToClip) {
  pLocalToClipRow = localToClip.Row;

  __m128 mat0 = localToClip.Row[0];
  __m128 mat1 = localToClip.Row[1];
  __m128 mat2 = localToClip.Row[2];
  __m128 mat3 = localToClip.Row[3];

  __m128 plane0 = _mm_add_ps(mat3, mat0);
  __m128 plane2 = _mm_add_ps(mat3, mat1);
  __m128 plane5 = _mm_sub_ps(mat3, mat2);

  // Bake viewport transform into matrix
  m_localToClip[0] = _mm_mul_ps_scalar_soc(
      plane0,
      static_cast<float>(m_width >> 1));  // m_width must be multiple of 16
  m_localToClip[1] = _mm_mul_ps_scalar_soc(
      plane2,
      static_cast<float>(m_height >> 1));  // m_height must be multiple of 8

  // Map depth from [-1, 1] to [bias, 0]
  m_localToClip[2] = _mm_mul_ps_scalar_soc(plane5, 0.5f * floatCompressionBias);

  m_localToClip[3] = mat3;

  _MM_TRANSPOSE4_PS(m_localToClip[0], m_localToClip[1], m_localToClip[2],
                    m_localToClip[3]);

  ////_MM_TRANSPOSE4_PS(mat0, mat1, mat2, mat3);

  ////// Store prebaked cols
  ////m_localToClip->LocalToClip.Row[0] = mat0;
  ////m_localToClip->LocalToClip.Row[1] = mat1;
  ////m_localToClip->LocalToClip.Row[2] = mat2;
  ////m_localToClip->LocalToClip.Row[3] = mat3;
}

/*
 * Recomputes interleave-mode block ranges for the current resolution. This is
 * called after resolution setup and after coherent-mode configuration changes.
 */
void Rasterizer::ConfigCoherent() {
  if (this->m_blocksX > 0 && this->m_blocksY > 0) {
    this->mInterleave.Config(this->m_blocksX, this->m_blocksY);
  }
}

/*
 * Starts rasterizer-side frame state for a new global frame. It resets valid
 * occluder counters and prepares optional occludee metadata collection for
 * debug depth-map overlays.
 */
void Rasterizer::ConfigGlobalFrameNum(uint64_t frameNum) {
  this->mGlobalFrameNum = frameNum;
  this->mCurrValidOccluderNum = 0;

  ShowOccludeeInDepthMap = ShowOccludeeInDepthMapNext;

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { ShowOccludeeInDepthMap = true; });

  if (ShowOccludeeInDepthMap) {
    mOccludeeResults.clear();
    mCurrentOccludee = -1;
  }
}
/*
 * Clears the HiZ min/max spans for a horizontal block range across every row.
 * Coherent interleave mode uses this to refresh only the half of the screen
 * that will be redrawn this frame.
 */
static inline void ZeroHizUpdateBlocks(uint16_t* pHiz, uint32_t m_blocksX,
                                       uint32_t m_blocksY, uint32_t blockNum,
                                       uint32_t m_HizBufferSize) {
  uint32_t size = blockNum * sizeof(uint16_t);

  uint32_t blockY = 0;
  while (blockY < m_blocksY) {
    memset(pHiz, 0, size);
    memset(pHiz + m_HizBufferSize, 0, size);
    pHiz += m_blocksX;
    ++blockY;
  }
}

/*
 * Prepares the depth buffers immediately before rasterizing submitted
 * occluders. Full frames clear all HiZ data, while interleaved frames clear
 * and clamp only the active horizontal side.
 */
void Rasterizer::ConfigBeforeRasterization() {
  mBlockWidthMin = 0;
  mBlockWidthMax = m_blocksX - 1;
  if (mInterleave.CurrentFrameInterleaveDrawing == false) {
    m_MaxCoord_WHWHOccluder = m_MaxCoord_WHWH;
    m_MinCoord_WHWHOccluder = _mm_setzero_si128();
    memset(m_pHiz, 0,
           m_HizBufferSize *
               sizeof(uint32_t));  // hizMin + hizMax, each one is uint16_t
  } else {
    mInterleave.InterleaveFrame++;
    mInterleave.mRenderRight = mInterleave.InterleaveFrame & 1;

    int PixelXMin = 0;
    int PixelXMax = m_width - 1;

    if (mInterleave.mRenderRight) {
      mBlockWidthMin = mInterleave.mBlock_XRightStart;
      PixelXMin = mInterleave.mPixel_XRightStart;
    } else {
      mBlockWidthMax = mInterleave.mBlock_XLeftEnd;
      PixelXMax = mInterleave.mPixel_XLeftEnd;
    }
    ZeroHizUpdateBlocks(m_pHiz + mBlockWidthMin, this->m_blocksX,
                        this->m_blocksY, mBlockWidthMax - mBlockWidthMin + 1,
                        m_HizBufferSize);

    m_MaxCoord_WHWHOccluder =
        _mm_setr_epi32(PixelXMax, m_height - 1, PixelXMax, m_height - 1);
    m_MinCoord_WHWHOccluder = _mm_setr_epi32(PixelXMin, 0, PixelXMin, 0);
  }
  if (mDebugRenderType != 0) {
    m_depthBufferPointLines.resize(m_totalPixels);
    memset(&m_depthBufferPointLines[0], 0, sizeof(uint16_t) * m_totalPixels);
  }
}
/*
 * Returns the coverage-mask storage for the selected checkerboard half inside
 * a packed block. The helper centralizes the PairBlockNum layout convention.
 */
static inline uint64_t* GetMaskData(uint64_t* data, int bit) {
  if (PairBlockNum == PureCheckerBoardApproach) return nullptr;
  // 0->8  1--> 0
  return data + ((1 ^ bit) << 3);
}

/*
 * Returns the depth-row storage for a checkerboard or full block pair. The bit
 * parameter selects the active half when the block stores two interleaved
 * checkerboard samples.
 */
static inline __m128i* GetDepthData(uint64_t* data, int bit) {
  if (PairBlockNum == PureCheckerBoardApproach) return (__m128i*)(data);

  return (__m128i*)(data + bit);
}

/*
 * Reorders a checkerboard bit mask between black/white pixel layouts. This is
 * used when recovering partial block data for depth-map readback and boundary
 * clipping.
 */
static uint64_t CheckerBoardTransform(uint64_t mask) {
  // start from row/column 1
  static constexpr uint64_t blackPattern = 0x55AA55AA55AA55AA;
  static constexpr uint64_t whitePattern = 0xAA55AA55AA55AA55;
  static constexpr uint64_t oddColumn = 0xAAAAAAAAAAAAAAAA;
  static constexpr uint64_t evenColumn = 0x5555555555555555;

  static constexpr uint64_t oddBlack = (oddColumn & blackPattern);
  static constexpr uint64_t evenBlack = (evenColumn & blackPattern);
  static constexpr uint64_t evenWhite = (evenColumn & whitePattern);
  static constexpr uint64_t oddWhite = (oddColumn & whitePattern);

  uint64_t oddBlackMask = mask & oddBlack;
  uint64_t evenBlackMask = mask & evenBlack;
  uint64_t evenWhiteMask = mask & evenWhite;
  uint64_t oddWhiteMask = mask & oddWhite;

  uint64_t even = evenWhiteMask | (oddWhiteMask >> 1);
  uint64_t odd =
      oddBlackMask | (evenBlackMask << 1);  // only black pixel are drawn

  return even | odd;
}

/*
 * Applies optional checkerboard visualization to an expanded 8x8 block. It can
 * force black/white patterns or binary output for debugging depth recovery.
 */
static void GrayCheckBoardWhitePixel(__m128i* TargetBlockData) {
  uint16_t value = 65535;
  if (bDumpCheckerboardImageBinary) {
    uint16_t* row = (uint16_t*)TargetBlockData;
    uint16_t binary[2];
    binary[0] = 0;
    binary[1] = value;
    for (int i = 0; i < 64; i++) {
      row[i] = binary[row[i] > 0];
    }
    return;
  }
  if (bDumpCheckerboardImageBlack) {
    value = 0;
  }
  if (bDumpCheckerboardImage) {
    uint16_t* row = (uint16_t*)TargetBlockData;
    for (int i = 0; i < 8; i++) {
      if (i & 1) {
        row[0] = row[2] = row[4] = row[6] = value;
      } else {
        row[1] = row[3] = row[5] = row[7] = value;
      }
      row += 8;
    }
  }
}
/*
 * Expands a partially updated checkerboard block into eight full depth rows for
 * readback. Missing samples are filled conservatively from the available block
 * minimum so debug images remain readable.
 */
static void RecoverPartialBlockData(__m128i* TargetBlockData,
                                    __m128i* depthRows, uint64_t* pbitMask) {
  if (PairBlockNum != PureCheckerBoardApproach) {
    __m128i min = _mm_set1_epi32(-1);
    for (int i = 0; i < 4; i++) {
      __m128i input = _mm_cmpeq_epi16(depthRows[i], _mm_set1_epi32(0));
      input = _mm_or_si128(input, depthRows[i]);
      min = _mm_min_epu16(min, input);
    }
    uint16_t minValue = _mm_min_epu16(min);
    if (_mm_min_epu16(min) == 65535)  // all zero
    {
      // CULLING_ENGINE_LOG_DEBUG("Set zero-min data block to white");
      // minValue = 255 << 8;
      if (SupportDepthTill65K)
        minValue = 52 << 9;
      else
        minValue = 180 << 8;
    }
    __m128i defaultV = _mm_set1_epi16(minValue);
    for (int i = 0; i < 4; i++) {
      __m128i t = _mm_srli_epi32(depthRows[i], 16);
      __m128i a = _mm_or_si128(t, _mm_slli_epi32(t, 16));
      t = _mm_slli_epi32(depthRows[i], 16);
      __m128i b = _mm_or_si128(t, _mm_srli_epi32(t, 16));

      __m128i zero = _mm_set1_epi32(0);
      __m128i a0b = _mm_and_si128(b, _mm_cmpeq_epi16(a, zero));

      __m128i r = _mm_or_si128(a, a0b);
      a0b = _mm_and_si128(defaultV, _mm_cmpeq_epi16(r, zero));

      TargetBlockData[(i << 1) | 1] = _mm_or_si128(r, a0b);
      a0b = _mm_and_si128(a, _mm_cmpeq_epi16(b, zero));

      r = _mm_or_si128(b, a0b);
      a0b = _mm_and_si128(defaultV, _mm_cmpeq_epi16(r, zero));

      TargetBlockData[(i << 1)] = _mm_or_si128(r, a0b);
    }
    if (SupportDepthTill65K) {
      for (int i = 0; i < 8; i++) {
        TargetBlockData[i] = _mm_srli_epi32(TargetBlockData[i], 1);
        TargetBlockData[i] =
            _mm_or_si128(TargetBlockData[i], _mm_set1_epi32(0x80008000));
      }
    }
    if (bDumpCheckerboardImage) {
      GrayCheckBoardWhitePixel(TargetBlockData);
    }

    __m128i interleavedBlockMask =
        _mm_unpacklo_epi8_soc(CheckerBoardTransform(pbitMask[0]));
    // in case of checkerboard sample, the recovered depth might not be correct
    TargetBlockData[0] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[0]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[1] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[1]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[2] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[2]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[3] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[3]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[4] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[4]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[5] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[5]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[6] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[6]);
    interleavedBlockMask = _mm_slli_epi16(interleavedBlockMask, 1);
    TargetBlockData[7] = _mm_and_si128(_mm_srai_epi16(interleavedBlockMask, 15),
                                       TargetBlockData[7]);
  } else {
    TargetBlockData[0] = depthRows[0];
    TargetBlockData[1] = depthRows[0];
    TargetBlockData[2] = depthRows[1];
    TargetBlockData[3] = depthRows[1];
    TargetBlockData[4] = depthRows[2];
    TargetBlockData[5] = depthRows[2];
    TargetBlockData[6] = depthRows[3];
    TargetBlockData[7] = depthRows[3];
  }
}

/*
 * Finalizes occluder rendering for the frame. It updates the last-occluder
 * count, clears stale HiZ state when no occluders rendered, and builds the row
 * bit-scan acceleration mask used by narrow occludee queries.
 */
void Rasterizer::OnOccluderRenderFinish() {
  // time to expand the depth map horizontally to cull more pixels

  int previousOcc = mLastOccluderNum;
  mLastOccluderNum = mCurrValidOccluderNum;
  if (mCurrValidOccluderNum == 0) {
    // if (previousOcc != 0 && this->mInterleave.CurrentFrameInterleaveDrawing)
    // //this one has bug, m_pHiz might leak
    if (previousOcc != 0) {
      memset(m_pHiz, 0, m_HizBufferSize * sizeof(uint32_t));
      if (bOccludeeBitScanOp) {
        memset(&mAnyDataBlockMask[0], 0, m_blocksY * sizeof(uint64_t));
      }
    }
    return;
  }

  //*****************************************************************************
  // calculate bin scan mask for first 1024 width, block 0 to block 127
  //*****************************************************************************

  if (mWidthIn1024 && bOccludeeBitScanOp)  // width must be size of 64
  {
    auto Time1 = std::chrono::high_resolution_clock::now();

    int maxXStep = m_blocksX;
    uint32_t y = 0;

    // For interleave mode, only Half would be needed to updated.
    // 32 * 64 = 2K operations could be saved!!
    uint64_t LastMask = 0;
    if (mInterleave.CurrentFrameInterleaveDrawing == true) {
      int DualBlockMinX = mBlockWidthMin >> 1;
      int DualBlockMaxX = mBlockWidthMax >> 1;
      uint64_t all = -1;
      uint64_t updateMask =
          (all << DualBlockMinX) & (all >> (63 - DualBlockMaxX));

      LastMask = all ^ updateMask;
    }

    int StartBlock = (mBlockWidthMin >> 3) << 3;
    uint32_t EndBlock = ((mBlockWidthMax + 7) >> 3) << 3;
    if (EndBlock > mBlockWidthMax) EndBlock -= 8;

    uint16_t* hizBufferStart = m_pHiz + StartBlock;
    int EndBlockIdx = EndBlock >> 1;
    int StartBlockIdx = StartBlock >> 1;  // the start update idx of uint64_t
    do {
      uint64_t rowMaskData0 = 0;  // only care 0~63
      {
        __m128i* CurrentHIZ = (__m128i*)hizBufferStart;
        int x = StartBlockIdx;
        uint16_t th = bOccludeeMinDepthThreshold;
        do {
          __m128i pass = _mm_cmple_epu16_soc(_mm_set1_epi16(th), CurrentHIZ[0]);
          __m128i pass2 = _mm_srai_epi32(pass, 16);
          pass = _mm_and_si128(pass, pass2);
          uint32_t* result = (uint32_t*)&pass;

          uint32_t maskValue = (result[0] & 1) | (result[1] & 2) |
                               (result[2] & 4) | (result[3] & 8);

          rowMaskData0 |= (uint64_t)(maskValue) << x;
          x += 4;
          CurrentHIZ++;  // = 8;
        } while (x <= EndBlockIdx);
      }

      mAnyDataBlockMask[y] &= LastMask;
      mAnyDataBlockMask[y] |= rowMaskData0;

      hizBufferStart += maxXStep;

      y++;
    } while (y < m_blocksY);

    auto Time2 = std::chrono::high_resolution_clock::now();

    bool verify = 0;
    if (verify) {
      int maxXStep = m_width >> 3;
      uint16_t* hizBuffer = m_pHiz;
      uint32_t y = 0;

      do {
        uint64_t rowMaskData0 = 0;  // only care 0~63
        {
          uint16_t* CurrentHIZ = (uint16_t*)hizBuffer;
          int x = 0;
          uint16_t th = bOccludeeMinDepthThreshold;
          do {
            int k = x >> 1;
            rowMaskData0 |=
                ((uint64_t)(CurrentHIZ[0] >= th && CurrentHIZ[1] >= th)) << k;
            k++;
            rowMaskData0 |=
                ((uint64_t)(CurrentHIZ[2] >= th && CurrentHIZ[3] >= th)) << k;
            k++;
            rowMaskData0 |=
                ((uint64_t)(CurrentHIZ[4] >= th && CurrentHIZ[5] >= th)) << k;
            k++;
            rowMaskData0 |=
                ((uint64_t)(CurrentHIZ[6] >= th && CurrentHIZ[7] >= th)) << k;
            x += 8;
            CurrentHIZ += 8;
          } while (x < maxXStep);
        }

        hizBuffer += maxXStep;
        assert(mAnyDataBlockMask[y] == rowMaskData0);

        y++;
      } while (y < m_blocksY);

      auto Time3 = std::chrono::high_resolution_clock::now();
      int totalNSNEON32 =
          (int)std::chrono::duration_cast<std::chrono::nanoseconds>(Time3 -
                                                                    Time2)
              .count();
      int totalNSNEON21 =
          (int)std::chrono::duration_cast<std::chrono::nanoseconds>(Time2 -
                                                                    Time1)
              .count();
      CULLING_ENGINE_LOG_DEBUG("HiZ mask timing: phase21=%d ns phase32=%d ns",
                               totalNSNEON21, totalNSNEON32);
    }
  }
}
