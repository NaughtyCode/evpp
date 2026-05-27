/*
 * Builds the rasterization mask lookup tables used by block traversal. The
 * table precomputes edge-slope and offset coverage masks so drawTriangle and
 * drawQuad can update 8x8 blocks without recomputing per-pixel half-plane
 * masks at runtime.
 */
void Rasterizer::PrecomputeRasterizationTable() {
  static constexpr uint32_t angularResolution = 2000;
  static constexpr uint32_t offsetResolution = 2000;

  __m128i* maskTable = nullptr;
  std::unique_ptr<__m128i[]> maskTableStorage;

  if (this->m_depthBuffer.capacity() >=
      OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR) {
    maskTable = (__m128i*)m_pDepthBuffer;
  } else {
    maskTableStorage = std::make_unique<__m128i[]>(OFFSET_QUANTIZATION_FACTOR *
                                                   SLOPE_QUANTIZATION_FACTOR);
    maskTable = maskTableStorage.get();
  }
  uint8_t* offsetLookupTable = nullptr;
  std::unique_ptr<uint8_t[]> offsetLookupTableStorage;
  if (this->m_depthBuffer.capacity() >=
      OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR +
          offsetResolution / 16) {
    offsetLookupTable =
        (uint8_t*)(m_pDepthBuffer +
                   OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR);
  } else {
    offsetLookupTableStorage = std::make_unique<uint8_t[]>(offsetResolution);
    offsetLookupTable = offsetLookupTableStorage.get();
  }

  if (ReflectBoostBlockMask_Optimization)
    memset(maskTable, 0,
           OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR / 2 *
               sizeof(__m128i));
  else
    memset(maskTable, 0,
           OFFSET_QUANTIZATION_FACTOR * SLOPE_QUANTIZATION_FACTOR *
               sizeof(__m128i));

  std::chrono::high_resolution_clock::time_point startTime =
      std::chrono::high_resolution_clock::now();

  // it has been found that 0~261 equals 0
  memset(offsetLookupTable, 0, 262 * sizeof(uint8_t));
  for (uint32_t i = 262; i < offsetResolution; ++i) {
    float offset = -0.6f + 1.2f * float(i) / (angularResolution - 1);
    offsetLookupTable[i] = QuantizeOffsetLookup(offset);  //[0, 63]
  }

  float xCache[8];
  float yCache[8];
  __m128i sumJThread[8];
  __m128i sumJThreadByte[8];
  __m128i sumJThreadK8[4];
  // 288648 / 17528 = 16.467. current approach is 16 times faster compare with
  // default cpp version

  int totalAngle = angularResolution;
  int fromAngle = 0;
  if (ReflectBoostBlockMask_Optimization) {
    totalAngle = angularResolution / 4 + 21;
    fromAngle = 32;
  }
  for (int angleValue = fromAngle; angleValue <= totalAngle; ++angleValue) {
    float angle = -0.1f + 6.4f * float(angleValue) / (angularResolution - 1);

    float nx = std::cos(angle);
    float ny = std::sin(angle);
    float absNxNy = (std::abs(nx) + std::abs(ny));
    float l = 1.0f / absNxNy;

    nx *= l;
    ny *= l;

    auto nxTemp = _mm_set1_ps(nx);
    auto nyTemp = _mm_set1_ps(ny);
    uint32_t slopeLookup =
        _mm_extract_epi32(QuantizeSlopeLookup(nxTemp, nyTemp), 0);
    if (ReflectBoostBlockMask_Optimization) slopeLookup >>= 1;

    if (true)  // 17481ns
    {
      auto t0 = _mm_setr_ps((0 - 3.5f) * 0.125f, (1 - 3.5f) * 0.125f,
                            (2 - 3.5f) * 0.125f, (3 - 3.5f) * 0.125f);
      auto t1 = _mm_setr_ps((4 - 3.5f) * 0.125f, (5 - 3.5f) * 0.125f,
                            (6 - 3.5f) * 0.125f, (7 - 3.5f) * 0.125f);
      __m128 xCache128[2];
      __m128 yCache128[2];
      nyTemp = _mm_negate_ps_soc(nyTemp);
      xCache128[0] = _mm_mul_ps(t0, nxTemp);
      xCache128[1] = _mm_mul_ps(t1, nxTemp);
      yCache128[0] = _mm_mul_ps(t0, nyTemp);
      yCache128[1] = _mm_mul_ps(t1, nyTemp);
      _mm_store_ps(xCache, xCache128[0]);
      _mm_store_ps(xCache + 4, xCache128[1]);
      _mm_store_ps(yCache, yCache128[0]);
      _mm_store_ps(yCache + 4, yCache128[1]);
    } else  // 20068ns
    {
      for (auto j = 0; j < 8; ++j) {
        auto t = (j - 3.5f) * 0.125f;
        xCache[j] = t * nx;
        yCache[j] = -t * ny;
      }
    }

    float min = -3.5f * 0.125f * absNxNy;

    // here calculate max offset resolution
    // min > 0.6f - 1.2f * float(i) / (angularResolution - 1);   //ignore
    // i > (0.6 - min) * (angularResolution - 1) / 1.2f; //ignore
    int maxj = (int)ceil((0.6 - min) * (angularResolution - 1) / 1.2f);
    maxj = std::min<int>(maxj, offsetResolution);

    auto y0 = _mm_load_ps(yCache);
    auto y4 = _mm_load_ps(yCache + 4);

    //	//i > (0.6 - sum) * (angularResolution - 1) / 1.2f; //ignore
    //	int maxj = ceil((600 - 1000 *min) * 1.999 / 1.2f);
    // maxj  map to [0, angularResolution + PositiveOffset]
    static constexpr int32_t PositiveOffset = 1;
    auto PositiveOffsetV = _mm_set1_ps(PositiveOffset);
    auto ps06 = _mm_set1_ps(0.6f);
    auto scale = _mm_set1_ps((angularResolution - 1) / 1.2f);
    for (auto j = 0; j < 8; ++j) {
      auto xm = _mm_set1_ps(xCache[j]);
      auto sum1 = _mm_add_ps(y0, xm);
      sum1 = _mm_sub_ps(ps06, sum1);
      sum1 = _mm_mul_ps(sum1, scale);

      sum1 = _mm_add_ps(sum1, PositiveOffsetV);
      sum1 = _mm_max_ps(sum1, _mm_set1_ps(0));
      sum1 = _mm_min_ps(sum1, _mm_set1_ps(angularResolution + PositiveOffset));

      __m128i sumInThread1 = _mm_cvttps_epi32(sum1);  // this is floor operation

      auto sum2 = _mm_add_ps(y4, xm);
      sum2 = _mm_sub_ps(ps06, sum2);
      sum2 = _mm_mul_ps(sum2, scale);

      sum2 = _mm_add_ps(sum2, PositiveOffsetV);
      sum2 = _mm_max_ps(sum2, _mm_set1_ps(0));
      sum2 = _mm_min_ps(sum2, _mm_set1_ps(angularResolution + PositiveOffset));
      __m128i sumInThread2 = _mm_cvttps_epi32(sum2);  // floor operation

      sumInThread2 = _mm_slli_epi32(sumInThread2, 16);
      sumJThread[j] = _mm_or_si128(sumInThread2, sumInThread1);
    }

    // maxj = 0;
    int minj = 0;
    if (ReflectBoostBlockMask_Optimization)
      minj = 256;  // hack to eliminate calculation

    if (bMaskTableOffsetOptimization) {
      static constexpr float th = 0.45f;
      static constexpr int minj2 =
          (int)((-th + 0.6) / 1.2f * (angularResolution - 1) - 2);
      static constexpr int maxj2 =
          (int)((th + 0.6) / 1.2f * (angularResolution - 1) + 2);
      maxj = std::min<int>(maxj, maxj2);
      minj = std::max<int>(minj, minj2);
    }

    for (int32_t j = minj; j < maxj; ++j) {
      int j127 = j & 127;  // map j to [0~127]
      if (j127 == 0) {
        int delta = j - (j & 127);
        __m128i deltaV = _mm_set1_epi16(delta);
        // map sumJThread to [0, 200];
        auto Byte128V = _mm_set1_epi16(128);
        for (int k = 0; k < 8; k++) {
          auto kd = _mm_sub_epi16(sumJThread[k], deltaV);
          auto k2 = _mm_cmple_epu16_soc(deltaV, sumJThread[k]);
          kd = _mm_and_si128(kd, k2);                       //[0
          sumJThreadByte[k] = _mm_min_epu16(kd, Byte128V);  //[0, 128]
        }
        sumJThreadK8[0] = _mm_or_si128(sumJThreadByte[0],
                                       _mm_slli_epi16(sumJThreadByte[1], 8));
        sumJThreadK8[1] = _mm_or_si128(sumJThreadByte[2],
                                       _mm_slli_epi16(sumJThreadByte[3], 8));
        sumJThreadK8[2] = _mm_or_si128(sumJThreadByte[4],
                                       _mm_slli_epi16(sumJThreadByte[5], 8));
        sumJThreadK8[3] = _mm_or_si128(sumJThreadByte[6],
                                       _mm_slli_epi16(sumJThreadByte[7], 8));
      }

      __m128i j8 = _mm_set1_epi8(j127);

      __m128i mask = _mm_cmplt_epu8_soc(j8, sumJThreadK8[0]);
      __m128i result = _mm_srli_epi8(mask, 7);
      mask = _mm_cmplt_epu8_soc(j8, sumJThreadK8[1]);
      result = _mm_or_si128(result, _mm_slli_epi8(_mm_srli_epi8(mask, 7), 1));
      mask = _mm_cmplt_epu8_soc(j8, sumJThreadK8[2]);
      result = _mm_or_si128(result, _mm_slli_epi8(_mm_srli_epi8(mask, 7), 2));
      mask = _mm_cmplt_epu8_soc(j8, sumJThreadK8[3]);
      result = _mm_or_si128(result, _mm_slli_epi8(_mm_srli_epi8(mask, 7), 3));

      uint32_t offsetLookup = offsetLookupTable[j];
      uint32_t lookup = slopeLookup | offsetLookup;
      maskTable[lookup] = _mm_or_si128(maskTable[lookup], result);  // block;
    }

    SocAssert(_mm_test_all_zeros(
                  _mm_sub_epi32(maskTable[slopeLookup], _mm_set1_epi8(0xF)),
                  _mm_set1_epi32(0xFFFFFFFF)) == 1);

    // triangle block update request the last index mask to be zero
    // otherwise, for thin vertical triangle, the convex optimization might
    // cause top block not updated
    SocAssert(_mm_test_all_zeros(maskTable[slopeLookup + 63],
                                 _mm_set1_epi32(0xFFFFFFFF)) == 1);

    //// For each slope, the first block should be all ones, the last all zeroes
  }

  uint64_t rows[4];

  int startY = 0;
  if (ReflectBoostBlockMask_Optimization) startY = 32;
  uint64_t* pMask = &m_precomputedRasterTables[0] + startY * 64;
  // calculate row 32 to row 62
  int yOffset = 1 + (int)ReflectBoostBlockMask_Optimization;
  int pYOffset = (int)ReflectBoostBlockMask_Optimization * 64;
  for (int y = startY; y < 64; y += yOffset, pMask += pYOffset) {
    int yBase = y << 6;  // y * 64
    if (ReflectBoostBlockMask_Optimization) yBase >>= 1;
    // it has been found that for case x <= 1, the mask value is -1. Fully
    // covered *pMask = -1; *pMask = -1;
    pMask += 1;
    for (int x = 1; x <= 63; x++) {
      int idx = yBase | x;
      auto& lookup = maskTable[idx];
#if !defined(CULLING_ENGINE_ARM)
      rows[0] = uint64_t(_mm_extract_epi32(lookup, 0));
      rows[1] = uint64_t(_mm_extract_epi32(lookup, 1));
      rows[2] = uint64_t(_mm_extract_epi32(lookup, 2));
      rows[3] = uint64_t(_mm_extract_epi32(lookup, 3));
#else
      // For iOS & Android
      uint32x4_t& lookupArray = *(uint32x4_t*)&lookup;
      rows[0] = lookupArray[0];
      rows[1] = lookupArray[1];
      rows[2] = lookupArray[2];
      rows[3] = lookupArray[3];
#endif
      uint64_t block = 0;
      for (int j = 0; j < 4; j++) {
        // 15 7 11 3      14 6 10 2      13 5  9 1         12 4  8 0    //each
        // contain 8 bit, the first 4 is the mask...
        uint64_t rowBlock = (rows[0] & 1);  // extra pos 0
        rowBlock |= (rows[1] & 1) << 1;     // extra pos 1
        rowBlock |= (rows[2] & 1) << 2;     // extra pos 2
        rowBlock |= (rows[3] & 1) << 3;     // extra pos 3

        rowBlock |= (rows[0] & (1 << 16)) >> 12;  // extra pos 4
        rowBlock |= (rows[1] & (1 << 16)) >> 11;  // extra pos 5
        rowBlock |= (rows[2] & (1 << 16)) >> 10;  // extra pos 6
        rowBlock |= (rows[3] & (1 << 16)) >> 9;   // extra pos 7

        rowBlock |= (rows[0] & (1 << 8));       // extra pos 8
        rowBlock |= (rows[1] & (1 << 8)) << 1;  // extra pos 9
        rowBlock |= (rows[2] & (1 << 8)) << 2;  // extra pos 10
        rowBlock |= (rows[3] & (1 << 8)) << 3;  // extra pos 11

        rowBlock |= (rows[0] & (1 << 24)) >> 12;  // extra pos 12
        rowBlock |= (rows[1] & (1 << 24)) >> 11;  // extra pos 13
        rowBlock |= (rows[2] & (1 << 24)) >> 10;  // extra pos 14
        rowBlock |= (rows[3] & (1 << 24)) >> 9;   // extra pos 15

        uint64_t perRow = rowBlock << (j << 4);
        if (block != 0 && perRow == 0) {
          break;
        }
        block |= perRow;

        rows[0] >>= 1;
        rows[1] >>= 1;
        rows[2] >>= 1;
        rows[3] >>= 1;
      }

      *pMask = block;
      pMask++;
    }
  }

  if (ReflectBoostBlockMask_Optimization) {
    uint8_t verticalReflectMap[256];
    for (int i = 0; i < 128; i++) {
      int n = i;
      // n = (128 & n) >> 7 | (64 & n) >> 5 | (32 & n) >> 3 | (16 & n) >> 1 | (8
      // & n) << 1 | (4 & n) << 3 | (2 & n) << 5 | (1 & n) << 7;
      n = (n & 0xaa) >> 1 | (n & 0x55) << 1;
      n = (n & 0xcc) >> 2 | (n & 0x33) << 2;
      n = (n & 0xf0) >> 4 | (n & 0x0f) << 4;
      verticalReflectMap[i] = n;
      verticalReflectMap[i | 128] = n | 1;
    }

    pMask = &m_precomputedRasterTables[0];
    pMask += 33 * 64;

    // time to calculate all the odd rows
    for (int y = 33; y < 64; y += 2, pMask += 128) {
      // for case x = 0, the mask value is -1. Fully covered
      for (int x = 1; x <= 63; x++) {
        uint64_t mirror = *(pMask + x - 64);
        uint8_t* mirrow8 = (uint8_t*)(&mirror);
        uint64_t current = 0;
        uint8_t* current8 = (uint8_t*)(&current);
        for (int bx = 0; bx < 8; bx++) {
          current8[bx] = verticalReflectMap[mirrow8[bx]];
        }
        pMask[x] = current;
      }
    }

    // x mirror reflection to calculate 0~31
    pMask = &m_precomputedRasterTables[0];
    for (int y = 0; y < 32; y += 1, pMask += 64) {
      // it has been found that for case x <= 1, the mask value is -1. Fully
      // covered *pMask = -1; pMask++; *pMask = -1; pMask++;

      uint64_t* targetRow = &m_precomputedRasterTables[(62 - y) * 64];
      targetRow += (y & 1) << 7;  // for odd,   change to 64 - y
      for (int x = 1; x <= 63; x++) {
        pMask[x] = CullingEngine_bswap_64(targetRow[x]);
      }
    }
    pMask = &m_precomputedRasterTables[0];
    for (int y = 0; y < 64; y++, pMask += 64) {
      pMask[0] = -1;
    }

    // here check the completeness
    bool completenessCheck = false;
    if (completenessCheck) {
      pMask = &m_precomputedRasterTables[0];
      for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
          int start = x + y * 64;

          int y0 = y ^ 1;
          if (y0 & 1)
            y0 = 64 - y0;
          else
            y0 = 62 - y0;

          int end = 63 - x + y0 * 64;
          uint64_t result = pMask[start] | pMask[end];
          if (result != -1) {
            CULLING_ENGINE_LOG_WARNING(
                "Raster table completeness check failed");
          }
        }
      }
    }

    // 9~54 difference mask elements
    // if (false) {
    //	pMask = &m_precomputedRasterTables[0];
    //	for (int y = 0; y < 64; y++, pMask += 64)
    //	{
    //		int same = 0;
    //		for (int y = 0; y < 63; y++)
    //		{
    //			same += pMask[y] == pMask[y + 1];
    //		}
    //		CULLING_ENGINE_LOG_DEBUG("Raster table row diff: row=%d diff=%d",
    // y, 64 - same);
    //	}
    // }
    pMask = &m_precomputedRasterTables[0];

    //////grant more visible pixels by comparing with cpp version
    ////m_precomputedRasterTables[151] |= 18446744073692774400U;
    ////m_precomputedRasterTables[166] |= 18446743523953737728U;
    ////m_precomputedRasterTables[215] |= 18446744073692774400U;
    ////m_precomputedRasterTables[400] |= 18446744073709547520U;
    ////m_precomputedRasterTables[422] |= 18446743936270598144U;
    ////m_precomputedRasterTables[464] |= 18446744073709489920U;
    ////m_precomputedRasterTables[486] |= 18446743004262694912U;
    ////m_precomputedRasterTables[784] |= 18446744073709353184U;
    ////m_precomputedRasterTables[848] |= 18446744073696911111U;
    ////m_precomputedRasterTables[923] |= 18446742961194582144U;
    ////m_precomputedRasterTables[936] |= 18372699527942504448U;
    ////m_precomputedRasterTables[987] |= 18446602507813585665U;
    ////m_precomputedRasterTables[1000] |= 9160056689884397568U;
    ////m_precomputedRasterTables[1201] |= 16204163117414875136U;
    ////m_precomputedRasterTables[1265] |= 506376785949097984U;
    ////m_precomputedRasterTables[1305] |= 18374401693156700400U;
    ////m_precomputedRasterTables[1307] |= 18373838726023409888U;
    ////m_precomputedRasterTables[1369] |= 9187131305195671311U;
    ////m_precomputedRasterTables[1371] |= 9169116769247235847U;
    ////m_precomputedRasterTables[1457] |= 16195156193046429696U;
    ////m_precomputedRasterTables[1521] |= 505250894632255488U;
    ////m_precomputedRasterTables[1711] |= 16195156194124415168U;
    ////m_precomputedRasterTables[1722] |= 9259542123265392640U;
    ////m_precomputedRasterTables[1775] |= 505250894665941763U;
    ////m_precomputedRasterTables[1786] |= 72340172838010880U;
    ////m_precomputedRasterTables[1923] |= 18446744073709485822U;
    ////m_precomputedRasterTables[1958] |= 17357120220336021728U;
    ////m_precomputedRasterTables[1965] |= 16204198715729174752U;
    ////m_precomputedRasterTables[1987] |= 18446744073701130111U;
    ////m_precomputedRasterTables[2022] |= 1082841962169960199U;
    ////m_precomputedRasterTables[2029] |= 506381209866536711U;
    ////m_precomputedRasterTables[4035] |= 288230376151711743U;
  }
  // initialize the Primitive Boundary Mask

  if (PairBlockNum <= PureCheckerBoardApproach + 1) {
    int total = 64 * 64;

    pMask = &m_precomputedRasterTables[0];

    for (int idx = 0; idx < total; idx++) {
      pMask[idx] = CheckerBoardTransform(pMask[idx]);
    }
  }

  if (false)  // neon version is around 200 times faster
  {
    auto endTime = std::chrono::high_resolution_clock::now();
    int totalNSNEON =
        (int)std::chrono::duration_cast<std::chrono::microseconds>(endTime -
                                                                   startTime)
            .count();
    CULLING_ENGINE_LOG_DEBUG(
        "Raster table precompute time: %d us",
        (int)std::chrono::duration_cast<std::chrono::microseconds>(endTime -
                                                                   startTime)
            .count());

    {
      std::vector<uint64_t> m_precomputedRasterTablesNEON;
      m_precomputedRasterTablesNEON.resize(m_precomputedRasterTables.size(), 0);
      std::swap(m_precomputedRasterTablesNEON, m_precomputedRasterTables);
      startTime = std::chrono::high_resolution_clock::now();
      for (uint32_t i = 0; i < angularResolution; ++i) {
        float angle = -0.1f + 6.4f * float(i) / (angularResolution - 1);

        float nx = std::cos(angle);
        float ny = std::sin(angle);
        float l = 1.0f / (std::abs(nx) + std::abs(ny));

        float nxo = nx;  // original
        float nyo = ny;  // original
        nx *= l;
        ny *= l;

        uint32_t slopeLookup = _mm_extract_epi32(
            QuantizeSlopeLookup(_mm_set1_ps(nx), _mm_set1_ps(ny)), 0);

        for (uint32_t j = 0; j < offsetResolution; ++j) {
          float offset = -0.6f + 1.2f * float(j) / (angularResolution - 1);

          uint32_t offsetLookup = QuantizeOffsetLookup(offset);

          uint32_t lookup = slopeLookup | offsetLookup;

          uint64_t block = 0;

          for (auto x = 0; x < 8; ++x) {
            for (auto y = 0; y < 8; ++y) {
              // groundtruth equation
              // float edgeDistance = offset + (x - 3.5f) / 8.0f * nx + (y
              // - 3.5f) / 8.0f * ny; rasterizer uses (0.5, 0.5).
              float edgeDistance = offset / l + (x - 3.5f) / 8.0f * nxo +
                                   (y - 3.5f) / 8.0f * nyo;
              if (edgeDistance <= 0.0f) {
                uint32_t bitIndex = 8 * x + (7 - y);
                block |= uint64_t(1) << bitIndex;
              }
            }
          }

          m_precomputedRasterTables[lookup] |= block;
        }
        // For each slope, the first block should be all ones, the last all
        // zeroes
        SocAssert(m_precomputedRasterTables[slopeLookup] == -1);
        // current row idx 1 is not -1 and row idx 63 is not 0
        //	socAssert(m_precomputedRasterTables[slopeLookup +
        // OFFSET_QUANTIZATION_FACTOR_MINUS_ONE] == 0);
      }

      endTime = std::chrono::high_resolution_clock::now();
      int totalCPP = (int)std::chrono::duration_cast<std::chrono::microseconds>(
                         endTime - startTime)
                         .count();

      int diffCount = 0;
      int largeCount = 0;
      for (int idx = 0; idx < m_precomputedRasterTablesNEON.size(); idx++) {
        if (m_precomputedRasterTablesNEON[idx] !=
            m_precomputedRasterTables[idx]) {
          int idx64 = idx & 63;
          if (idx64 == 0 || idx64 == 1 || idx64 == 63) continue;

#if defined(CULLING_ENGINE_NATIVE)
          CULLING_ENGINE_LOG_DEBUG(
              "m_precomputedRasterTables[%d] = %lluU;", idx,
              static_cast<unsigned long long>(m_precomputedRasterTables[idx]));
#endif
          diffCount++;
        }
        if (m_precomputedRasterTablesNEON[idx] >
            m_precomputedRasterTables[idx]) {
          largeCount++;
        }
      }

      // Use NEON input
      std::swap(m_precomputedRasterTablesNEON, m_precomputedRasterTables);

      CULLING_ENGINE_LOG_DEBUG(
          "Raster table comparison: cppToNeonRatio=%lf diffCount=%d "
          "largeCount=%d cppTime=%d us neonTime=%d us",
          totalCPP * 1.0f / totalNSNEON, diffCount, largeCount, totalCPP,
          totalNSNEON);
    }
  }
}
