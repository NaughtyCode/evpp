/*
 * Applies hierarchy short-circuiting for occludee queries. If a parent is
 * invisible, all following child results are forced invisible and the caller is
 * told how many child entries can be skipped.
 */
static inline unsigned int OnParentNodeQuery(unsigned int i_mesh,
                                             unsigned int n_mesh, bool* results,
                                             uint16_t* mOccludeeTreeData) {
  uint16_t treeSize = mOccludeeTreeData[i_mesh];
  if (treeSize > 1 && results[i_mesh] == false) {
    unsigned int childNum = treeSize - 1;
    const unsigned int remaining = i_mesh < n_mesh ? n_mesh - i_mesh - 1u : 0u;
    childNum = std::min(childNum, remaining);
    memset(results + i_mesh + 1, 0, childNum * sizeof(bool));
    return childNum;
  }
  return 0;
}

/*
 * Executes a batched occludee visibility query, optionally honoring query-tree
 * parent/child metadata. The template parameters keep tree and
 * width-specialized paths branch-free inside the per-box loop.
 */
template <bool bHasTreeData, bool OccludeeWidth1024>
void Rasterizer::BatchQueryWithTree(const float* bbox, unsigned int nMesh,
                                    bool* results, QueryDebugStates* outErr) {
  const float* min = bbox;
  if (mOccludeeTrueAsCulled == false) {
    if (ShowOccludeeInDepthMap == false) {
      for (unsigned int idx = 0; idx < nMesh; ++idx, min += BBOX_STRIDE) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kCurrentOccludeeIdx] = idx; });

        results[idx] = QueryVisibility<false, OccludeeWidth1024>(min, outErr);
        if (bHasTreeData) {
          unsigned int skipNum =
              OnParentNodeQuery(idx, nMesh, results, this->mOccludeeTreeData);
          idx += skipNum;
          min += skipNum * BBOX_STRIDE;
        }

        CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(
            outErr, "batchQueryWithTree1", results[idx]);
      }
    } else {
      for (unsigned int idx = 0; idx < nMesh; ++idx, min += BBOX_STRIDE) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kCurrentOccludeeIdx] = idx; });

        results[idx] = QueryVisibility<false, OccludeeWidth1024>(min, outErr);

        if (bHasTreeData) {
          unsigned int skipNum =
              OnParentNodeQuery(idx, nMesh, results, this->mOccludeeTreeData);
          idx += skipNum;
          min += skipNum * BBOX_STRIDE;
        }

        if (mCurrentOccludee != -1) {
          mOccludeeResults.push_back(mCurrentOccludee);
          if (SupportDepthTill65K) {
            mCurrentOccludeeDepth >>= 9;
            mCurrentOccludeeDepth |= 128;
          } else {
            mCurrentOccludeeDepth >>= 8;
          }

          mOccludeeResults.push_back(((uint64_t)mCurrentOccludeeDepth << 48) |
                                     (uint64_t)results[idx]);
          mCurrentOccludee = -1;
        }

        CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(
            outErr, "batchQueryWithTree2", results[idx]);
      }
    }
  } else {
    if (ShowOccludeeInDepthMap == false) {
      for (unsigned int idx = 0; idx < nMesh; ++idx, min += BBOX_STRIDE) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kCurrentOccludeeIdx] = idx; });

        if (results[idx] == false) {
          results[idx] = QueryVisibility<false, OccludeeWidth1024>(min, outErr);
        } else {
          results[idx] = false;
        }

        if (bHasTreeData) {
          unsigned int skipNum =
              OnParentNodeQuery(idx, nMesh, results, this->mOccludeeTreeData);
          idx += skipNum;
          min += skipNum * BBOX_STRIDE;
        }

        CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(
            outErr, "batchQueryWithTree3", results[idx]);
      }
    } else {
      for (unsigned int idx = 0; idx < nMesh; ++idx, min += BBOX_STRIDE) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { DebugData[kCurrentOccludeeIdx] = idx; });

        if (results[idx] == false) {
          results[idx] = QueryVisibility<false, OccludeeWidth1024>(min, outErr);
          if (bHasTreeData) {
            unsigned int skipNum =
                OnParentNodeQuery(idx, nMesh, results, this->mOccludeeTreeData);
            idx += skipNum;
            min += skipNum * BBOX_STRIDE;
          }

          if (mCurrentOccludee != -1) {
            mOccludeeResults.push_back(mCurrentOccludee);
            if (SupportDepthTill65K) {
              mCurrentOccludeeDepth >>= 9;
              mCurrentOccludeeDepth |= 128;
            } else {
              mCurrentOccludeeDepth >>= 8;
            }

            mOccludeeResults.push_back(((uint64_t)mCurrentOccludeeDepth << 48) |
                                       (uint64_t)results[idx]);
            mCurrentOccludee = -1;
          }
        } else {
          results[idx] = false;
        }

        CULLING_ENGINE_DUMP_AND_RESET_QUERY_VISIBILITY_RESULTS(
            outErr, "batchQueryWithTree4", results[idx]);
      }
    }
  }
}

/*
 * Public batch-query entry point for width-specialized occludee testing. It
 * dispatches to the tree or non-tree implementation and resets one-shot query
 * state after the batch completes.
 */
template <bool OccludeeWidth1024>
void Rasterizer::BatchQuery(const float* bbox, unsigned int nMesh,
                            bool* results) {
  if (bDebugOccluderOnly) {
    memset(DebugData, 0, DEBUG_DATA_SIZE * sizeof(uint32_t));
    return;
  }

  QueryDebugStates* debugStates = nullptr;

#if CULLING_ENGINE_ENABLE_BATCH_QUERY_DUMP

  CULLING_ENGINE_LOG_DEBUG("Rasterizer debug: batch query");

  QueryDebugStates debugStatesLocal;
  debugStates = &debugStatesLocal;

#endif  // End of CULLING_ENGINE_ENABLE_BATCH_QUERY_DUMP

  if (this->mOccludeeTreeData == nullptr) {
    BatchQueryWithTree<false, OccludeeWidth1024>(bbox, nMesh, results,
                                                 debugStates);
  } else {
    BatchQueryWithTree<true, OccludeeWidth1024>(bbox, nMesh, results,
                                                debugStates);
    mOccludeeTreeData = nullptr;
  }

  mOccludeeTrueAsCulled = false;

#if CULLING_ENGINE_ENABLE_RASTERIZER_DEBUG
  DumpAll(bbox, results, nMesh);
#endif  // End of CULLING_ENGINE_ENABLE_RASTERIZER_DEBUG
}

/*
 * Estimates memory held by the rasterizer and its variable-size buffers. This
 * is intentionally capacity based so callers can understand retained memory
 * after earlier larger resolutions or debug modes.
 */
size_t Rasterizer::GetMemorySizeInBytes() const {
  size_t memorySizeInBytes = 0;

  // self
  memorySizeInBytes += sizeof(Rasterizer);

  // Members
  {
    memorySizeInBytes += m_precomputedRasterTables.capacity() * sizeof(int64_t);
    memorySizeInBytes += (m_depthBuffer.capacity() * sizeof(__m128i));

    if (mPrimitiveBoundaryClip) {
      memorySizeInBytes += sizeof(PrimitiveBoundaryClipCache);
    }
    if (DebugData != nullptr) {
      memorySizeInBytes += DEBUG_DATA_SIZE * sizeof(uint32_t);
    }

    memorySizeInBytes += mOccludeeResults.capacity() * sizeof(uint64_t);
    memorySizeInBytes += m_depthBufferPointLines.capacity() * sizeof(uint16_t);
    memorySizeInBytes += mAnyDataBlockMask.capacity() * sizeof(int64_t);
  }

  return memorySizeInBytes;
}

/*
 * Tests a world-space AABB against the current clip-space frustum planes.
 * boundsMin and boundsMax are expected to carry XYZ plus W=1, while extents
 * contains the AABB size used to select conservative plane offsets.
 */
bool Rasterizer::InFrustum(__m128& boundsMin, __m128& boundsMax,
                           __m128& extents) {
  if (bFrustumCullIfClip == false) return true;
  // Bounding box center times 2 - but since W = 2, the plane equations work out
  // correctly
  __m128 center = _mm_add_ps(boundsMax, boundsMin);

  __m128 minusZero = _mm_set1_ps(-0.0f);
  __m128* FrustumPlane = this->m_FrustumPlane;

  // Compute distance from each frustum plane
  __m128 offset0 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[0], minusZero)));
  bool dist0 = _mm_dp_ps_float_soc(FrustumPlane[0], offset0) >= 0;

  __m128 offset1 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[1], minusZero)));
  bool dist1 = _mm_dp_ps_float_soc(FrustumPlane[1], offset1) >= 0;

  __m128 offset2 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[2], minusZero)));
  bool dist2 = _mm_dp_ps_float_soc(FrustumPlane[2], offset2) >= 0;

  __m128 offset3 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[3], minusZero)));
  bool dist3 = _mm_dp_ps_float_soc(FrustumPlane[3], offset3) >= 0;

  __m128 offset4 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[4], minusZero)));
  bool dist4 = _mm_dp_ps_float_soc(FrustumPlane[4], offset4) >= 0;

  __m128 offset5 = _mm_add_ps(
      center, _mm_xor_ps(extents, _mm_and_ps(FrustumPlane[5], minusZero)));
  bool dist5 = _mm_dp_ps_float_soc(FrustumPlane[5], offset5) >= 0;

  // Combine plane distance signs
  bool combined = (dist0 & dist1) & (dist2 & dist3) & (dist4 & dist5);
  return combined;
}

/*
 * Projects an occluder or occludee AABB and decides whether it is potentially
 * visible. For occluders it also caches transform edges needed by subsequent
 * rasterization when the bounds pass the visibility test.
 */
template <bool bQueryOccluder, bool bOccludeeWidth1024>
bool Rasterizer::QueryVisibility(const float* minmaxf,
                                 QueryDebugStates* outErr) {
  // Frustum cull
  __m128 extents;
  if (bQueryOccluder) {
    extents = _mm_setr_ps(minmaxf[3], minmaxf[4], minmaxf[5], 0);
  } else {
    extents = _mm_setr_ps(minmaxf[3] - minmaxf[0], minmaxf[4] - minmaxf[1],
                          minmaxf[5] - minmaxf[2], 0);
  }

  // Transform edges
  __m128 egde0 = _mm_mul_ps_scalar_soc(m_localToClip[0], extents, 0);
  __m128 egde1 = _mm_mul_ps_scalar_soc(m_localToClip[1], extents, 1);
  __m128 egde2 = _mm_mul_ps_scalar_soc(m_localToClip[2], extents, 2);
  __m128 corners[8];

  // Transform first corner
  corners[0] = _mm_fmadd_ps(
      m_localToClip[0], _mm_set1_ps(minmaxf[0]),
      _mm_fmadd_ps(m_localToClip[1], _mm_set1_ps(minmaxf[1]),
                   _mm_fmadd_ps(m_localToClip[2], _mm_set1_ps(minmaxf[2]),
                                m_localToClip[3])));

  if (bQueryOccluder) {
    mOccluderCache.mat[3] = corners[0];
  }

  // Transform remaining corners by adding edge vectors
  corners[1] = _mm_add_ps(corners[0], egde0);
  corners[2] = _mm_add_ps(corners[0], egde1);
  corners[4] = _mm_add_ps(corners[0], egde2);

  corners[3] = _mm_add_ps(corners[1], egde1);
  corners[5] = _mm_add_ps(corners[4], egde0);
  corners[6] = _mm_add_ps(corners[2], egde2);

  // corners[7] = _mm_add_ps(corners[6], egde0); //same logic, less depdent on
  // previous instruction
  corners[7] = _mm_add_ps(corners[3], egde2);
  // Transpose into SoA
  _MM_TRANSPOSE4_PS(corners[0], corners[1], corners[2], corners[3]);
  _MM_TRANSPOSE4_PS(corners[4], corners[5], corners[6], corners[7]);

  bool requireClip = false;
  // Even if all bounding box corners have W > 0 here, we may end up with some
  // vertices with W < 0 to due floating point differences; so test with some
  // epsilon if any W < 0.
  if (bQueryOccluder) {
    // issue: if the model size is large, the previous epsilon would be
    // unnecessary large, which makes the clipping is often true...
    //////__m128 cornerMaxZ = _mm_max_ps(corners[3], corners[7]);
    //////cornerMaxZ = _mm_max4_ps_soc(cornerMaxZ);
    //////__m128 nearPlaneEpsilon = _mm_mul_ps_scalar_soc(cornerMaxZ, 0.001f);

    //////__m128 closeToNearPlane = _mm_cmplt_ps(_mm_min_ps(corners[3],
    /// corners[7]), nearPlaneEpsilon);
    ////////check occluder.cpp bakePureTriangle related code, it would be sure
    /// that points are inside
    //////the original AABB
    __m128 minW = _mm_min_ps(corners[3], corners[7]);

    __m128 closeToNearPlane = _mm_cmplt_ps(minW, _mm_set1_ps(mNearPlane));
    requireClip = !_mm_same_sign0(closeToNearPlane);

    // clipping is for rasterization use only
    mOccluderCache.NeedsClipping = requireClip;

    // VERIFICATION with previous approach
    ////__m128 maxExtent = _mm_max_ps(extents, _mm_shuffle_ps(extents, extents,
    ///_MM_SHUFFLE(1, 0, 3, 2))); /maxExtent = _mm_max_ps(maxExtent,
    ///_mm_shuffle_ps(maxExtent, maxExtent, _MM_SHUFFLE(2, 3, 0, 1)));
    ////nearPlaneEpsilon = _mm_mul_ps_scalar_soc(maxExtent, 0.001f);
    //////__m128 closeToNearPlane = _mm_or_ps(_mm_cmplt_ps(corners[3],
    /// nearPlaneEpsilon), _mm_cmplt_ps(corners[7], nearPlaneEpsilon));
    ////closeToNearPlane = _mm_cmplt_ps(_mm_min_ps(corners[3], corners[7]),
    /// nearPlaneEpsilon); /bool requireClip2 =
    ///!_mm_same_sign0(closeToNearPlane); /if (requireClip != requireClip2)
    ////{
    ////	CULLING_ENGINE_LOG_DEBUG("CLIP DIFF  %d  vs old %d",
    ///(int)requireClip, (int)requireClip2);
    ////}
  } else {
    if (OCCLUDEE_NEARCLIP_BBOX_CHECK_IGNORE_OPTIMIZATION)  // it might not
                                                           // needed
    {
      __m128 closeToNearPlane = _mm_cmplt_ps(_mm_min_ps(corners[3], corners[7]),
                                             _mm_set1_ps(mNearPlane));
      requireClip = !_mm_same_sign0(closeToNearPlane);
    } else {
      // the calculation is for occludees only, occluders' nearPlaneEpsilon is
      // pre-calculated
      __m128 maxExtent = _mm_max4_ps_soc(extents);
      __m128 nearPlaneEpsilon = _mm_mul_ps_scalar_soc(maxExtent, 0.001f);
      //__m128 closeToNearPlane = _mm_or_ps(_mm_cmplt_ps(corners[3],
      // nearPlaneEpsilon), _mm_cmplt_ps(corners[7], nearPlaneEpsilon));
      __m128 closeToNearPlane =
          _mm_cmplt_ps(_mm_min_ps(corners[3], corners[7]), nearPlaneEpsilon);
      requireClip = !_mm_same_sign0(closeToNearPlane);
    }
  }

  bool visible = requireClip;
  CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(visible,
                                                   kQueryVisibilityInit);

  if (requireClip == false)  // visibility still unknown
  {
    // Perspective division
    corners[3] = _mm_rcp_ps(corners[3]);
    corners[0] = _mm_mul_ps(corners[0], corners[3]);
    corners[1] = _mm_mul_ps(corners[1], corners[3]);
    corners[2] = _mm_mul_ps(corners[2], corners[3]);

    corners[7] = _mm_rcp_ps(corners[7]);
    corners[4] = _mm_mul_ps(corners[4], corners[7]);
    corners[5] = _mm_mul_ps(corners[5], corners[7]);
    corners[6] = _mm_mul_ps(corners[6], corners[7]);

    // Vertical mins and maxes
    __m128 minsX = _mm_min_ps(corners[0], corners[4]);
    __m128 maxsX = _mm_max_ps(corners[0], corners[4]);

    __m128 minsY = _mm_min_ps(corners[1], corners[5]);
    __m128 maxsY = _mm_max_ps(corners[1], corners[5]);
    __m128i minsXY = _mm_cvttps_epi32(_mm_min_ps(
        _mm_unpacklo_ps(minsX, minsY), _mm_unpackhi_ps(minsX, minsY)));
    __m128i maxsXY = _mm_cvttps_epi32(_mm_max_ps(
        _mm_unpacklo_ps(maxsX, maxsY), _mm_unpackhi_ps(maxsX, maxsY)));

    // stress test. put in inf number to check whether crash
    // minsX = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));
    // maxsX = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));
    // minsY = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));
    // maxsY = _mm_castsi128_ps(_mm_set1_epi32(0x7f800000));

    // Clamp bounds

    if (bQueryOccluder == false) {
      __m128i minThreshold = _mm_set1_epi32(0);

      minsXY = _mm_max_epi32(minsXY, minThreshold);
      // already frustum culled. limit the max of minXY
      maxsXY = _mm_min_epi32(maxsXY, m_MaxCoordOccludee_WHWH);
    } else {
      // add safe force clamp to allow safe traversal of blocks
      minsXY = _mm_max_epi32(minsXY, m_MinCoord_WHWHOccluder);
      maxsXY = _mm_min_epi32(maxsXY, m_MaxCoord_WHWHOccluder);
    }

    // Horizontal reduction, step 2
    __m128i minXYMaxXYLo = _mm_unpacklo_epi32(minsXY, maxsXY);
    __m128i minXYMaxXYHi = _mm_unpackhi_epi32(minsXY, maxsXY);

    __m128i boundsI[2];
    boundsI[0] = _mm_min_epi32(minXYMaxXYLo, minXYMaxXYHi);
    boundsI[1] = _mm_max_epi32(minXYMaxXYLo, minXYMaxXYHi);

    int* bounds = (int*)boundsI;

    // frustum culling
    // very useful for interleave mode occluder
    {
      if (bounds[0] > bounds[5] || bounds[2] > bounds[7]) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
            { this->DebugData[kOccluderCulled] += bQueryOccluder; });

        CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(
            false, kQueryVisibilityFrustumCulling);

        return false;
      }
    }

    {
      uint32_t minX = bounds[0];
      uint32_t maxX = bounds[5];
      uint32_t minY = bounds[2];
      uint32_t maxY = bounds[7];

      __m128i depth = PackQueryDepth(_mm_max_ps(corners[2], corners[6]));
      uint16_t maxZ = _mm_max_epu16_even(depth);

      if (bQueryOccluder == false)  // for Occludee
      {
        if (ShowOccludeeInDepthMap) {
          mCurrentOccludee = ((uint64_t)minX << 48) | ((uint64_t)maxX << 32) |
                             ((uint64_t)minY << 16) | (uint64_t)maxY;

          mCurrentOccludeeDepth = maxZ;
        }

        {
          visible =
              Query2D<false, bOccludeeWidth1024>(minX, maxX, minY, maxY, maxZ);
          CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(
              visible, kQueryVisibilityNoneQueryOccluderQuery2d);
        }
      } else  // for occluder
      {
        visible = Query2D<bQueryOccluder, bOccludeeWidth1024>(minX, maxX, minY,
                                                              maxY, maxZ);
        CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(
            visible, kQueryVisibilityQueryOccluderQuery2d);
      }
    }

    if (bQueryOccluder) {
      if (visible) {
        mOccluderCache.PrepareCache(egde0, egde1, egde2);
      }
    }
  } else {
    if (bQueryOccluder == false) {
      __m128 depthJoint = _mm_and_ps(corners[3], corners[7]);
      if (_mm_same_sign1_soc(depthJoint)) {
        // test code: to verify that if behind the camera, in frustum must be
        // false
        if (false && bFrustumCullIfClip) {
          __m128 boundsMin =
              _mm_setr_ps(minmaxf[0], minmaxf[1], minmaxf[2], 1.0f);
          __m128 boundsMax;

          if (bQueryOccluder) {
            boundsMax = _mm_add_ps(boundsMin, extents);
            UpdateFrustumCullPlane();  // delay the frustum culling plane
                                       // preparation for occluders
          } else {
            boundsMax = _mm_setr_ps(minmaxf[3], minmaxf[4], minmaxf[5], 1.0f);
          }

          assert(InFrustum(boundsMin, boundsMax, extents) == false);
        }

        CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(
            false, kQueryVisibilityOccludee);

        return false;
      }
    }

    if (bFrustumCullIfClip) {
      __m128 boundsMin = _mm_setr_ps(minmaxf[0], minmaxf[1], minmaxf[2], 1.0f);
      __m128 boundsMax;

      if (bQueryOccluder) {
        boundsMax = _mm_add_ps(boundsMin, extents);
        UpdateFrustumCullPlane();  // delay the frustum culling plane
                                   // preparation for occluders
      } else {
        boundsMax = _mm_setr_ps(minmaxf[3], minmaxf[4], minmaxf[5], 1.0f);
      }

      if (mIsNeedCheckInFrustum && !InFrustum(boundsMin, boundsMax, extents)) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          if (bQueryOccluder == false) {
            this->DebugData[kOccludeeFrustumCull]++;
            this->DebugData[kOccludeeCull]++;
          } else {
            this->DebugData[kOccluderCulled]++;
            this->DebugData[kOccluderFrustumCulled]++;
          }
        });

        CULLING_ENGINE_MARK_LOCAL_QUERY_VISIBILITY_ERROR(
            false, kQueryVisibilityFrustumCullIfClipFrustum);

        return false;
      }
    }

    if (bQueryOccluder) {
      mOccluderCache.PrepareCache(egde0, egde1, egde2);
    }

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      if (bQueryOccluder) {
        this->DebugData[kOccludeeNearClipPass]++;
      }
    });
  }

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    this->DebugData[kOccluderCulled] += bQueryOccluder && (visible == false);
  });

  // refactor previous multiple exit to this single exit
  return visible;
}

/*
 * Returns true when the left mask contains a pixel pattern not present in the
 * right mask. Query code uses this to detect uncovered checkerboard samples.
 */
static inline bool ContainPattern10(uint64_t left, uint64_t right) {
  return left > (left & right);
  // return ((left ^ right) & left) > 0;
}
/*
 * Produces either 7 or 0 for branchless block-boundary masking. Passing 0 keeps
 * the local coordinate; passing 1 opens the full row or column span.
 */
static inline int Mask70(int idx) { return (7 + idx) & 7; }
/*
 * Performs the 2D HiZ and per-pixel query after an AABB has been projected into
 * screen-space bounds. The occluder path is conservative for PVS generation,
 * while the occludee path returns final visibility for the caller.
 */
template <bool bQueryOccluder, bool bOccludeeWidth1024>
bool Rasterizer::Query2D(uint32_t pixelMinX, uint32_t pixelMaxX,
                         uint32_t pixelMinY, uint32_t pixelMaxY,
                         uint16_t maxZ) {
  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    if (!bQueryOccluder) {
      DebugData[kMaxOccludeeZ] =
          std::max<uint16_t>(maxZ, DebugData[kMaxOccludeeZ]);
      DebugData[kOccludeeQuery2d]++;
    }
  });

  // X / 8 == X >> 3
  int blockMinX = pixelMinX >> 3;
  int blockMaxX = pixelMaxX >> 3;

  int blockMaxY = pixelMaxY >> 3;
  int blockMinY = pixelMinY >> 3;

  if (bQueryOccluder == false && bOccludeeBitScanOp) {
    // return true;
    if (maxZ < bOccludeeMinDepthThreshold && bOccludeeWidth1024) {
      // 1. occludee any block scan
      uint64_t all = -1;
      int DualBlockMinX = blockMinX >> 1;
      int DualBlockMaxX = blockMaxX >> 1;
      uint64_t rowMask = (all << DualBlockMinX) & (all >> (63 ^ DualBlockMaxX));

      int y = blockMaxY;
      uint64_t mask = -1;
      do {
        mask &= mAnyDataBlockMask[y];
        y--;
      } while (y >= blockMinY);

      if (rowMask == (rowMask & mask)) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          if (!bQueryOccluder) {
            DebugData[kFastRowBitCull]++;
          }
        });

        return false;
      }
    }
  }

  // usage of hiz Max to accelerate pass check

  int topRow = blockMaxY * m_blocksX;
  if (m_pHizMax[topRow + blockMinX] <= maxZ ||
      m_pHizMax[topRow + blockMaxX] <= maxZ)  // top left
  {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      DebugData[kOccludeeQueryMaxPass] += !bQueryOccluder;
      DebugData[kOccluderQueryMaxPass] += bQueryOccluder;
    });

    return true;
  }

  uint16_t* pHiZBuffer = m_pHiz;
  uint64_t* pDepthBuffer = m_pDepthBuffer;

  uint32_t pHizOffset = (blockMaxY * m_blocksX + blockMinX);  // top left
  // int blockRangeX = blockMaxX - blockMinX;

  {
    int startBlockY = -1;

    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
        { DebugData[kQueryBlockDoWhileIfSave]++; });

    int blockY = blockMaxY;
    do {
      uint16_t* pHiZ = pHiZBuffer + pHizOffset;

      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
          { DebugData[kQueryBlockDoWhileIfSave]++; });

      int blockX = blockMinX;
      do {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          if (!bQueryOccluder) {
            DebugData[kFastBlockDepthCompare]++;
          }
        });

        if (maxZ > pHiZ[0]) {
          if (pHiZ[0] == 0) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              if (!bQueryOccluder) {
                DebugData[kFastBlockEmptyPass]++;
              }
            });

            return true;
          }
          startBlockY = blockY;
          blockY = -1;
          break;
        }
        ++blockX;
        ++pHiZ;
      } while (blockX <= blockMaxX);
      --blockY;
      pHizOffset -= m_blocksX;
    } while (blockY >= blockMinY);

    if (startBlockY == -1) {
      CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
        if (!bQueryOccluder) {
          DebugData[kFastBlockHizCull]++;
          DebugData[kOccludeeCull]++;
        }
      });

      return false;
    }
    if (startBlockY != blockMaxY) {
      // make pixel Y end map always 7, means fully covered the block
      pixelMaxY = 7;
      blockMaxY = startBlockY;
    }
    pHizOffset += m_blocksX;
  }

  if (m_pHizMax[topRow + ((blockMaxX + blockMinX) >> 1)] <= maxZ)  // top left
  {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      DebugData[kOccludeeQueryMaxPass] += !bQueryOccluder;
      DebugData[kOccluderQueryMaxPass] += bQueryOccluder;
    });

    return true;
  }

  // Extend the width in case that only one pixel occludee
  // this should not affect performance much, as
  // 1. branchless code
  // 2. most cases are already determined
  if (PairBlockNum <= CheckerBoardVizMaskApproach) {
    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
      if (!bQueryOccluder) {
        DebugData[kOccludeeOnePixelExpandCheck]++;
      }
    });

    // branchless approach
    int OnePixelOccludee = (pixelMinX == pixelMaxX) && (pixelMinY == pixelMaxY);
    // case 0: OnePixelOccludee == 0, no impact to pixelMinX, pixelMaxX
    // case 1:
    //  the one pixel last bit is 0:  expand pixelMaxX
    //  the one pixel last bit is 1:  reduce pixelMinX
    pixelMaxX |= OnePixelOccludee;
    int lastBit = pixelMinX & 1;
    pixelMinX ^= lastBit & OnePixelOccludee;
    // branch approach
    // if (pixelMinX == pixelMaxX && pixelMinY == pixelMaxY)
    //{
    //	if ((pixelMinX & 7) == 0)
    //	{
    //		pixelMaxX++;
    //	}
    //	else
    //	{
    //		pixelMinX--;
    //	}
    // }
  }

  // up to here, pixelMinY pixelMaxY pixelMaxX pixelMinX are only used to
  // calculate per block's startY endY startX endX
  pixelMaxY &= 7;
  pixelMaxX &= 7;

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      { DebugData[kQueryBlockDoWhileIfSave]++; });

  int blockY = blockMaxY;
  do {
    uint16_t* pHiZ =
        pHiZBuffer + (pHizOffset);  // optimize pHizOffset = (blockY * m_blocksX
                                    // + blockMinX);
    uint64_t* pBlockDepth = pDepthBuffer + (pHizOffset * PairBlockNum);

    // uint16_t startY = 0;
    // if (blockY == blockMinY) startY = pixelMinY & 7;
    // uint16_t startY = (pixelMinY & 7) >> ((int)(blockY != blockMinY) << 2);
    // //branchless version
    uint16_t startY = pixelMinY & Mask70((int)(blockY != blockMinY));
    uint16_t endY = (pixelMaxY | Mask70((int)blockY == blockMaxY));

    // bool interiorLine = (startY == 0) && (endY == 7);

    for (int blockX = blockMinX; blockX <= blockMaxX;
         ++blockX, ++pHiZ, pBlockDepth += PairBlockNum) {
      uint16_t hiz = pHiZ[0];
      // Skip this block if it fully occludes the query box
      if (maxZ <= hiz) {
        continue;
      }

      if (hiz == 0) {
        CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
          if (!bQueryOccluder) {
            DebugData[kBlockEmptyPass] += hiz == 0;
          }
        });

        return true;
      }

      // uint16_t startX = 0;
      // if (blockX == blockMinX) startX = pixelMinX & 7;
      // uint16_t startX = (pixelMinX & 7) >> ((int)(blockX != blockMinX) << 2);
      // //branchless version
      uint16_t startX = pixelMinX & Mask70((int)(blockX != blockMinX));

      // uint16_t endX = 7;
      // if (blockX == blockMaxX) endX = pixelMaxX & 7;
      uint16_t endX = (pixelMaxX | Mask70((int)blockX == blockMaxX));

      // due to usage of conserve minz calculation. this check is not valid
      // anymore
      ////{
      ////    CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
      ////	{
      ////		if (!bQueryOccluder) {
      /// DebugData[kBlockCorrectMinPass]++; } /	}); /	if
      /// (interiorLine) / { /		bool
      /// interiorBlock = (startX == 0) && (endX == 7); /		// No
      /// pixels are masked, so there exists one where maxZ > pixelZ, and the
      /// query region is visible /		if (interiorBlock) / { /
      /// return true; /		} /	}
      ////}

      if (PairBlockNum <= PureCheckerBoardApproach + 1) {
        if (hiz <= MIN_UPDATED_BLOCK_DEPTH2 &&
            PairBlockNum != PureCheckerBoardApproach)  // partial updated block
        {
          uint64_t check = mPrimitiveBoundaryClip->PixelMinXMask[startX] &
                           mPrimitiveBoundaryClip->PixelMaxXMask[endX] &
                           mPrimitiveBoundaryClip->PixelMinYMask[startY] &
                           mPrimitiveBoundaryClip->PixelMaxYMask[endY];
          uint64_t* maskData = GetMaskData(pBlockDepth, blockX & 1);
          if (ContainPattern10(check, maskData[0])) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              if (!bQueryOccluder) {
                DebugData[kBlockMaskPass]++;
              }
            });

            return true;
          }
        }

        __m128i* startBlockDepth;
        if (PairBlockNum != PureCheckerBoardApproach)
          startBlockDepth = GetDepthData(pBlockDepth, (blockX & 1));
        else
          startBlockDepth = (__m128i*)pBlockDepth;

        int rowSelector = (0xFF << startX) & (0xFF >> (7 ^ endX));
        __m128i maxZV = _mm_set1_epi16(maxZ);

        {
          uint8_t* checkerBoardMask =
              (uint8_t*)(mCheckerBoardQueryMask +
                         (mCheckerBoardQueryOffset[startY] + endY - startY));
          //*******************************************************************

          uint16_t realStartY = startY >> 1;
          uint16_t realEndY = endY >> 1;

          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO(
              { DebugData[kQueryBlockDoWhileIfSave]++; });

          uint16_t y = realStartY;
          do {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              if (!bQueryOccluder) {
                DebugData[kBlockRowCheck]++;
              }
            });

            auto dy = startBlockDepth[y];
            __m128i visible = _mm_cmplt_epu16_soc(dy, maxZV);
            int visiblePixelMask = _mm_movemask_epi16_soc(visible);

            int currentRowSelector = rowSelector;
            if (currentRowSelector & visiblePixelMask & checkerBoardMask[y]) {
              CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
                if (!bQueryOccluder) {
                  DebugData[kBlockPixelPass]++;
                }
              });

              return true;
            }
            ++y;
          } while (y <= realEndY);
        }
      } else {
        __m128i maxZV = _mm_set1_epi16(maxZ);
        // int rowSelector = (0xFF << startX) & (0xFF >> (7 ^ endX)); //endX
        // falls in[0,7] 7^endX = 7-endX
        int rowSelector = (0xFF << startX) & (0xFF >> (7 ^ endX));

        __m128i* startBlockDepth = (__m128i*)pBlockDepth;
        for (uint16_t y = startY; y <= endY; ++y) {
          CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
            if (!bQueryOccluder) {
              DebugData[kBlockRowCheck]++;
            }
          });

          __m128i visible = _mm_cmplt_epu16_soc(startBlockDepth[y], maxZV);
          int visiblePixelMask = _mm_movemask_epi16_soc(visible);
          if (rowSelector & visiblePixelMask) {
            CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
              if (!bQueryOccluder) {
                DebugData[kBlockPixelPass]++;
              }
            });

            return true;
          }
        }
      }
    }
    --blockY;
    pHizOffset -= m_blocksX;
  } while (blockY >= blockMinY);

  CULLING_ENGINE_UPDATE_OCCLUDER_OCCLUDEE_DEBUG_INFO({
    if (!bQueryOccluder) {
      DebugData[kBlockPixelCull]++;
      DebugData[kOccludeeCull]++;
    }
  });

  // Not visible
  return false;
}
/*
 * Merges normal block readback data with optional debug point/line data. Valid
 * debug pixels are highlighted while existing depth samples are dimmed so both
 * render paths remain visible in one output image.
 */
static __m128i CompareWithExtraBuffer(__m128i blockData, __m128i extraRoot) {
  __m128i valid = _mm_cmplt_epu16_soc(_mm_setzero_si128(), extraRoot);
  __m128i match = _mm_cmple_epu16_soc(blockData, extraRoot);
  match = _mm_and_si128(valid, match);

  __m128i gray = _mm_srli_epi16(blockData, 1);
  __m128i gray2 = _mm_srli_epi16(blockData, 2);
  gray = _mm_adds_epu8(gray, gray2);

  return _mm_or_si128(gray, match);
}
