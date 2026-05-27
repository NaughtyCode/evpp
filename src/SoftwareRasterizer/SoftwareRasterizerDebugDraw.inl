/*
 * Draws a clipped debug line segment into the point/line depth buffer. The
 * function handles viewport clipping, endpoint-depth interpolation, and point
 * mode fallback for debug render types.
 */
void Rasterizer::DrawLine(float* p, float* q) {
  if (p[0] > q[0]) {
    std::swap(p, q);
  }

  if ((p[1] < 0 && q[1] < 0) || (p[1] >= m_height && q[1] >= m_height)) {
    return;
  }

  if ((p[0] < 0 && q[0] < 0) || (p[0] >= m_width && q[0] >= m_width)) {
    return;
  }

  {
    if (p[0] < 0) {
      float r = (0.5f - p[0]) / (q[0] - p[0]);
      p[2] = (q[2] - p[2]) * r + p[2];
      p[1] = (q[1] - p[1]) * r + p[1];
      p[0] = 0.5f;
    }
    if ((p[1] < 0 && q[1] < 0) || (p[1] >= m_height && q[1] >= m_height)) {
      return;
    }
    if (q[0] >= m_width) {
      float r = (m_width - 0.5f - p[0]) / (q[0] - p[0]);
      q[2] = (q[2] - p[2]) * r + p[2];
      q[1] = (q[1] - p[1]) * r + p[1];
      q[0] = (float)m_width - 0.5f;
    }
    if ((p[1] < 0 && q[1] < 0) || (p[1] >= m_height && q[1] >= m_height)) {
      return;
    }

    if (p[1] > q[1]) {
      std::swap(p, q);
    }

    if (p[1] < 0) {
      float r = (0.5f - p[1]) / (q[1] - p[1]);
      p[2] = (q[2] - p[2]) * r + p[2];
      p[0] = (q[0] - p[0]) * r + p[0];
      p[1] = 0.5f;
    }

    if ((p[0] < 0 && q[0] < 0) || (p[0] >= m_width && q[0] >= m_width)) {
      return;
    }

    if (q[1] >= m_height) {
      float r = ((float)m_height - 1 - p[1]) / (q[1] - p[1]);
      q[2] = (q[2] - p[2]) * r + p[2];
      q[0] = (q[0] - p[0]) * r + p[0];
      q[1] = (float)m_height - 0.5f;
    }

    if ((p[0] < 0 && q[0] < 0) || (p[0] >= m_width && q[0] >= m_width)) {
      return;
    }
  }

  p[2] = std::max<float>(p[2], MIN_PIXEL_DEPTH_FLOAT);
  q[2] = std::max<float>(q[2], MIN_PIXEL_DEPTH_FLOAT);

  if (mDebugRenderType == RenderType::kRenderPoint ||
      mDebugRenderType == RenderType::kRenderMeshPoint)  // 3 draw points only
  {
    DrawPixel((int)p[0], (int)p[1], p[2]);
    DrawPixel((int)q[0], (int)q[1], q[2]);
    return;
  }

  // line algorithm reference:
  // https://csustan.csustan.edu/~tom/Lecture-Notes/Graphics/Bresenham-Line/Bresenham-Line.pdf
  int x0 = (int)p[0];
  int y0 = (int)p[1];

  int x1 = (int)q[0];
  int y1 = (int)q[1];

  int dx = x1 - x0;
  int dy = y1 - y0;
  int stepx, stepy;

  if (dy < 0) {
    dy = -dy;
    stepy = -1;
  } else {
    stepy = 1;
  }
  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  } else {
    stepx = 1;
  }
  dy <<= 1; /* dy is now 2*dy */
  dx <<= 1; /* dx is now 2*dx */

  DrawPixel(x0, y0, p[2]);
  if (dx == 0 && dy == 0) {
    return;
  }

  if (x0 < 0 || x0 >= (int)m_width || y0 < 0 || y0 >= (int)m_height || x1 < 0 ||
      x1 >= (int)m_width || y1 < 0 || y1 >= (int)m_height) {
    return;
  }

  if (dx > dy) {
    float zSlope = (q[2] - p[2]) / (q[0] - p[0]);

    float p2p0zSlope = p[2] - p[0] * zSlope;
    int fraction = dy - (dx >> 1);
    while (x0 != x1) {
      x0 += stepx;
      if (fraction >= 0) {
        y0 += stepy;
        fraction -= dx;
      }
      fraction += dy;
      // drawPixelSafe(x0, y0, p[2] + (x0 - p[0]) * zSlope);
      DrawPixelSafe(x0, y0, x0 * zSlope + p2p0zSlope);
    }
  } else {
    float zSlope = (q[2] - p[2]) / (q[1] - p[1]);

    float p2p1zSlope = p[2] - p[1] * zSlope;
    int fraction = dx - (dy >> 1);
    while (y0 != y1) {
      if (fraction >= 0) {
        x0 += stepx;
        fraction -= dy;
      }
      y0 += stepy;
      fraction += dx;
      DrawPixelSafe(x0, y0, y0 * zSlope + p2p1zSlope);
      // drawPixelSafe(x0, y0, p[2] + (y0 - p[1]) * zSlope);
    }
  }
}

/*
 * Writes one already-clipped debug pixel. The caller must ensure x/y are inside
 * the framebuffer; this function only quantizes depth and merges it into the
 * point/line buffer.
 */
void Rasterizer::DrawPixelSafe(int x, int y, float zf) {
  int z = *(int*)&zf;
  z = z >> 12;

  z = std::min<uint16_t>(65535, z + 256);  // move forward the value

  int pixelIdx = y * this->m_width + x;
  m_depthBufferPointLines[pixelIdx] =
      std::max<uint16_t>(m_depthBufferPointLines[pixelIdx], z);
  if (bDebugOccluderOnly) {
    if (x == bDebugOccluderPixelX && y == bDebugOccluderPixelY) {
      this->DebugData[kBlockPacketPrimitiveDebug] = 1;
    }
  }
}

/*
 * Bounds-checking wrapper around drawPixelSafe for debug point output. Pixels
 * outside the framebuffer are ignored.
 */
void Rasterizer::DrawPixel(int x, int y, float zf) {
  // float maxf = decompressFloat(65535);
  // if (zf >= maxf) return;
  if (x < 0 || x >= (int)m_width || y < 0 || y >= (int)m_height) {
    return;
  }
  DrawPixelSafe(x, y, zf);

  // mapping pixel (x, y) to 64 bit
  // maskData[0] |= (uint64_t)1 << (8 * idxX + 7 - idxY);
  // maskData[0] |= (uint64_t)1 << ( (idxX<<3) | (7 ^ idxY));
}

/*
 * Emits triangle or quad wireframe/point debug geometry for every active SIMD
 * primitive lane. Triangle input converts inverse-W back to depth before the
 * debug draw path consumes it.
 */
template <int PrimitveEdgeNum>
void Rasterizer::HandleDrawMode(__m128* x, __m128* y, __m128* z,
                                uint32_t alivePrimitive) {
  __m128 z2[3];
  if (PrimitveEdgeNum == 3) {
    // reset the true data, here the z is actually invW. Set this way to make
    // the main code gap shorter
    __m128* invW = z;
    z = z2;

    __m128 c0 = mOccluderCache.c0;
    __m128 c1 = mOccluderCache.c1;
    z[0] = _mm_fmadd_ps(invW[0], c1, c0);
    z[1] = _mm_fmadd_ps(invW[1], c1, c0);
    z[2] = _mm_fmadd_ps(invW[2], c1, c0);
  }

  if (bDebugOccluderOnly) {
    if (-1 == bDebugOccluderPixelX && -1 == bDebugOccluderPixelY) {
      this->DebugData[kBlockPacketPrimitiveDebug] = 1;
    }
  }

  __m128 px[PrimitveEdgeNum];
  __m128 py[PrimitveEdgeNum];

  for (int i = 0; i < PrimitveEdgeNum; ++i) {
    px[i] = (_mm_mul_ps(x[i], _mm_set1_ps(8)));
    py[i] = (_mm_mul_ps(y[i], _mm_set1_ps(8)));
  }

  float* fpx = (float*)px;
  float* fpy = (float*)py;
  float* fpz = (float*)z;
  do {
    int j = alivePrimitive & 3;

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
        { this->DebugData[kBlockPacketPrimitive] = j; });

    alivePrimitive >>= 2;
    float p[3];
    float q[3];
    for (int i = 0; i < PrimitveEdgeNum; i++) {
      int idx = j | (i << 2);
      p[0] = fpx[idx];
      p[1] = fpy[idx];
      p[2] = fpz[idx];

      int k = (i + 1) % PrimitveEdgeNum;
      idx = j | (k << 2);
      q[0] = fpx[idx];
      q[1] = fpy[idx];
      q[2] = fpz[idx];

      if (p[2] > 0 && q[2] > 0) {
        DrawLine(p, q);
      } else {
        if (p[2] > 0) {
          DrawPixel((int)p[0], (int)p[1], p[2]);
        }
        if (q[2] > 0) {
          DrawPixel((int)q[0], (int)q[1], q[2]);
        }
      }
    }
  } while (alivePrimitive != 0);
}
