#include "CullingEngineLogger.h"

#include <cstdarg>

CullingEngineLogger CullingEngineLogger::Singleton;

void CullingEngineLogger::LogMain(const char* filename, int linenumber,
                                  const char* format, ...) {
  va_list args;
  va_start(args, format);
  CullingEngine::VLogFormatted(CullingEngine::Level::kInfo, filename,
                               linenumber, format, args);
  va_end(args);
}

void CullingEngineSetLogFunc(PFN_CullingEngineLogCallback logFunc) {
  CullingEngine::SetLogCallback(logFunc);
}
