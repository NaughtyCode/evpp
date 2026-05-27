#include "CullingEngineLogger.h"
#include "MathUtility.h"

#if CULLING_ENGINE_ENABLE_MATRIX4X4_DEBUG

void CullingEngine::Matrix4x4::Dump(const char* prefix) {
  SaveToData();

  CULLING_ENGINE_LOG_DEBUG(
      "Matrix4x4 %s: row0=(%f, %f, %f, %f) row1=(%f, %f, %f, %f) "
      "row2=(%f, %f, %f, %f) row3=(%f, %f, %f, %f)",
      prefix, m_Data[0][0], m_Data[0][1], m_Data[0][2], m_Data[0][3],
      m_Data[1][0], m_Data[1][1], m_Data[1][2], m_Data[1][3], m_Data[2][0],
      m_Data[2][1], m_Data[2][2], m_Data[2][3], m_Data[3][0], m_Data[3][1],
      m_Data[3][2], m_Data[3][3]);
}

#endif
