/*
 * Explicit template instantiations for all rasterizer variants used by the
 * dispatch paths. Keeping these in the same translation unit avoids exposing
 * private template definitions through public headers.
 */
template void Rasterizer::Rasterize<0>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<1>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<2>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<3>(CullingEngine::OccluderMesh& raw);
// template void Rasterizer::Rasterize<4>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<5>(CullingEngine::OccluderMesh& raw);
// template void Rasterizer::Rasterize<6>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<7>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<8>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<9>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<10>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<11>(CullingEngine::OccluderMesh& raw);
// template void Rasterizer::Rasterize<12>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<13>(CullingEngine::OccluderMesh& raw);
// template void Rasterizer::Rasterize<14>(CullingEngine::OccluderMesh& raw);
template void Rasterizer::Rasterize<15>(CullingEngine::OccluderMesh& raw);

template bool Rasterizer::QueryVisibility<true, false>(
    const float* minmaxf, QueryDebugStates* outErr);
template bool Rasterizer::QueryVisibility<false, true>(
    const float* minmaxf, QueryDebugStates* outErr);
template bool Rasterizer::QueryVisibility<false, false>(
    const float* minmaxf, QueryDebugStates* outErr);

template bool Rasterizer::Query2D<true, false>(uint32_t minX, uint32_t maxX,
                                               uint32_t minY, uint32_t maxY,
                                               uint16_t maxZ);
template bool Rasterizer::Query2D<false, true>(uint32_t minX, uint32_t maxX,
                                               uint32_t minY, uint32_t maxY,
                                               uint16_t maxZ);
template bool Rasterizer::Query2D<false, false>(uint32_t minX, uint32_t maxX,
                                                uint32_t minY, uint32_t maxY,
                                                uint16_t maxZ);

template void Rasterizer::BatchQueryWithTree<true, true>(
    const float* bbox, unsigned int nMesh, bool* results,
    QueryDebugStates* outErr);
template void Rasterizer::BatchQueryWithTree<true, false>(
    const float* bbox, unsigned int nMesh, bool* results,
    QueryDebugStates* outErr);
template void Rasterizer::BatchQueryWithTree<false, true>(
    const float* bbox, unsigned int nMesh, bool* results,
    QueryDebugStates* outErr);
template void Rasterizer::BatchQueryWithTree<false, false>(
    const float* bbox, unsigned int nMesh, bool* results,
    QueryDebugStates* outErr);

template void Rasterizer::BatchQuery<true>(const float* bbox,
                                           unsigned int nMesh, bool* results);
template void Rasterizer::BatchQuery<false>(const float* bbox,
                                            unsigned int nMesh, bool* results);

template void Rasterizer::HandleDrawMode<3>(__m128* x, __m128* y, __m128* z,
                                            uint32_t alivePrimitive);
template void Rasterizer::HandleDrawMode<4>(__m128* x, __m128* y, __m128* z,
                                            uint32_t alivePrimitive);

template void Rasterizer::DrawTriangle<true, true>(__m128* x, __m128* y,
                                                   __m128* invW, __m128* W,
                                                   __m128 primitiveValid);
template void Rasterizer::DrawTriangle<false, true>(__m128* x, __m128* y,
                                                    __m128* invW, __m128* W,
                                                    __m128 primitiveValid);
template void Rasterizer::DrawTriangle<true, false>(__m128* x, __m128* y,
                                                    __m128* invW, __m128* W,
                                                    __m128 primitiveValid);
template void Rasterizer::DrawTriangle<false, false>(__m128* x, __m128* y,
                                                     __m128* invW, __m128* W,
                                                     __m128 primitiveValid);

template void Rasterizer::DrawQuad<true>(__m128* x, __m128* y, __m128* invW,
                                         __m128* W, __m128 primitiveValid,
                                         __m128* edgeNormalsX,
                                         __m128* edgeNormalsY, __m128* areas);
// template void Rasterizer::DrawQuad<false>(__m128 * x, __m128 * y, __m128 *
// invW, __m128 * W, __m128 primitiveValid, __m128* edgeNormalsX, __m128*
// edgeNormalsY, __m128* areas);

template void Rasterizer::SplitToTwoTriangles<true, true>(__m128* X, __m128* Y,
                                                          __m128* W,
                                                          __m128* invW,
                                                          __m128 primitiveValid,
                                                          int validMask);
template void Rasterizer::SplitToTwoTriangles<true, false>(
    __m128* X, __m128* Y, __m128* W, __m128* invW, __m128 primitiveValid,
    int validMask);
template void Rasterizer::SplitToTwoTriangles<false, true>(
    __m128* X, __m128* Y, __m128* W, __m128* invW, __m128 primitiveValid,
    int validMask);
template void Rasterizer::SplitToTwoTriangles<false, false>(
    __m128* X, __m128* Y, __m128* W, __m128* invW, __m128 primitiveValid,
    int validMask);

/*
 * Precomputes pixel-boundary masks for clipping primitive coverage to a partial
 * 8x8 block. The masks are stored for all possible min/max X/Y edge positions
 * and transformed for checkerboard storage when that layout is active.
 */
void PrimitiveBoundaryClipCache::CalculateMask() {
  MinYBoundary[0] = -1;
  MaxYBoundary[0] = -1;
  MinXBoundary[0] = -1;
  MaxXBoundary[0] = -1;

  // rowMask |= 1 << (8 * x + (7 - y));
  uint64_t yMask = 255;
  PixelMinXMask[7] = yMask << (7 << 3);
  for (int x = 6; x >= 0; --x) {
    uint64_t current = yMask << (x << 3);
    PixelMinXMask[x] = current | PixelMinXMask[x + 1];
  }

  PixelMaxXMask[0] = yMask;
  for (int x = 1; x <= 7; ++x) {
    uint64_t current = yMask << (x << 3);
    PixelMaxXMask[x] = current | PixelMaxXMask[x - 1];
  }

  //		rowMask |= 1 << (8 * x + (7 - y));
  uint64_t xMask = 0;
  for (int x = 0; x <= 7; x++) {
    uint64_t rowMaskBit = 1;
    xMask |= rowMaskBit << (8 * x);
  }
  PixelMinYMask[7] = xMask;
  for (int y = 6; y >= 0; y--) {
    uint64_t current = xMask << (7 - y);
    PixelMinYMask[y] = current | PixelMinYMask[y + 1];
  }

  PixelMaxYMask[0] = xMask << (7 - 0);
  for (int y = 1; y <= 7; y++) {
    uint64_t current = xMask << (7 - y);
    PixelMaxYMask[y] = current | PixelMaxYMask[y - 1];
  }

  if (PairBlockNum <= PureCheckerBoardApproach + 1) {
    for (int idx = 0; idx <= 7; idx++) {
      PixelMinXMask[idx] = CheckerBoardTransform(PixelMinXMask[idx]);
      PixelMaxXMask[idx] = CheckerBoardTransform(PixelMaxXMask[idx]);
      PixelMinYMask[idx] = CheckerBoardTransform(PixelMinYMask[idx]);
      PixelMaxYMask[idx] = CheckerBoardTransform(PixelMaxYMask[idx]);
    }
  }
}

}  // namespace CullingEngine

/*
 * Enables or disables the global native debug path that records occluder and
 * occludee overlay information. This is process-wide because the debug flag is
 * a file-local rasterizer implementation setting.
 */
void CullingEngineSetIsDebugOccluderOccludee(bool bIsDebugOccluderOccludee) {
  CullingEngine::DebugOccluderOccludee = bIsDebugOccluderOccludee;
}

/*
 * Returns the current process-wide occluder/occludee debug overlay flag.
 */
bool CullingEngineGetGetIsDebugOccluderOccludee() {
  return CullingEngine::DebugOccluderOccludee;
}
