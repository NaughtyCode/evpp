#include "CullingEngineDebugConfiguration.h"
#include "CullingEngineLogger.h"

void QueryDebugStates::DumpAndReset(const char* prefix, int usrData) {
  if (InvisibleReason != kQueryVisibilityNone) {
    CULLING_ENGINE_LOG_DEBUG(
        "Query visibility: %s failed reason=%s visible=%d", prefix,
        GetErrorTypeString(InvisibleReason), usrData);
  }

  if (VisibleReason != kQueryVisibilityNone) {
    CULLING_ENGINE_LOG_DEBUG(
        "Query visibility: %s passed reason=%s visible=%d", prefix,
        GetErrorTypeString(VisibleReason), usrData);
  }

  Reset();
}

const char* QueryDebugStates::GetErrorTypeString(
    QueryVisibilityErrorType type) {
  switch (type) {
    case kQueryVisibilityNone:
      return "QueryVisibility_None";
    case kQueryVisibilityInit:
      return "QueryVisibility_Init";
    case kQueryVisibilityFrustumCulling:
      return "QueryVisibility_FrustumCulling";
    case kQueryVisibilityNoneQueryOccluderQuery2d:
      return "QueryVisibility_None_QueryOccluder_Query2D";
    case kQueryVisibilityQueryOccluderQuery2d:
      return "QueryVisibility_QueryOccluder_Query2D";
    case kQueryVisibilityOccludee:
      return "QueryVisibility_Occludee";
    case kQueryVisibilityFrustumCullIfClipFrustum:
      return "QueryVisibility_FrustumCullIfClip_Frustum";
  }

  return "QueryVisibility_None";
}
