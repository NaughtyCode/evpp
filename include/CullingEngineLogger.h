#pragma once

#include <cassert>
#include <string>

#include "CullingEngineAPI.h"
#include "CullingEngineLog.h"

class CullingEngineLogger {
 public:
  enum { kMaxLogBufferSize = 512 };

 public:
  static CullingEngineLogger Singleton;

 public:
  void AddLog(const std::string& msg) {
    CullingEngine::AddLog(msg, __FILE__, __LINE__);
  }

  bool GetLog(char* msgOut) { return CullingEngine::GetLog(msgOut); }

  bool PrintLog() { return CullingEngine::PrintLog(); }

  void SetStoreMsgConfig(bool store) { CullingEngine::SetStoreMessages(store); }

  static void LogMain(const char* filename, int linenumber, const char* format,
                      ...);
};

static inline void SocAssert(bool b) {
#if defined(CULLING_ENGINE_NATIVE)
  assert(b);
#else
  if (b == false) {
    CULLING_ENGINE_LOG_ERROR("SOC assert failed");
  }
#endif
}
