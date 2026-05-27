#include "CullingEngineLog.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CullingEngine {

constexpr std::size_t kInlineFormatBufferSize = 512;
constexpr std::size_t kMaxBufferedLogCount = 16;

class LoggerState {
 public:
  void SetLogCallback(PFN_CullingEngineLogCallback callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = callback;
  }

  void SetStoreMessages(bool enabled) {
    std::lock_guard<std::mutex> lock(mMutex);
    mStoreMessages = enabled;
  }

  void AddLog(std::string_view message, const char* filename, int line) {
    PFN_CullingEngineLogCallback callback = nullptr;
    bool storeMessages = false;
    {
      std::lock_guard<std::mutex> lock(mMutex);
      storeMessages = mStoreMessages;
      callback = mCallback;

      if (mBufferedLogs.size() < kMaxBufferedLogCount) {
        if (storeMessages) {
          mBufferedLogs.emplace_back(message);
          return;
        }
      }
    }

    if (callback != nullptr) {
      const std::string ownedMessage(message);
      callback(ownedMessage.c_str(), filename, line);
      return;
    }
  }

  bool GetLog(char* msgOut, std::size_t capacity) {
    if (msgOut == nullptr || capacity == 0) {
      return false;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (mBufferedLogs.empty()) {
      return false;
    }

    const std::string message = std::move(mBufferedLogs.front());
    mBufferedLogs.pop_front();

    const std::size_t count = std::min(capacity - 1, message.size());
    std::copy_n(message.data(), count, msgOut);
    msgOut[count] = '\0';
    return true;
  }

  bool PrintLog() {
    PFN_CullingEngineLogCallback callback = nullptr;
    std::string message;
    {
      std::lock_guard<std::mutex> lock(mMutex);
      if (mCallback == nullptr || mBufferedLogs.empty()) {
        return false;
      }

      callback = mCallback;
      message = std::move(mBufferedLogs.front());
      mBufferedLogs.pop_front();
    }

    callback(message.c_str(), __FILE__, __LINE__);
    return true;
  }

  void Log(Level level, const char* filename, int line,
           std::string_view message) {
    (void)level;

    PFN_CullingEngineLogCallback callback = nullptr;
    bool storeMessages = false;
    {
      std::lock_guard<std::mutex> lock(mMutex);
      callback = mCallback;
      storeMessages = mStoreMessages;
    }

    if (callback != nullptr) {
      const std::string ownedMessage(message);
      callback(ownedMessage.c_str(), filename, line);
      return;
    }

    if (storeMessages) {
      AddLog(message, filename, line);
      return;
    }

    std::clog << message << '\n';
  }

 private:
  std::mutex mMutex;
  std::deque<std::string> mBufferedLogs;
  PFN_CullingEngineLogCallback mCallback = nullptr;
  bool mStoreMessages = false;
};

LoggerState& State() {
  static LoggerState state;
  return state;
}

std::string FormatMessage(const char* format, va_list args) {
  if (format == nullptr) {
    return {};
  }

  std::array<char, kInlineFormatBufferSize> inlineBuffer{};
  va_list inlineArgs;
  va_copy(inlineArgs, args);
  const int inlineResult = std::vsnprintf(
      inlineBuffer.data(), inlineBuffer.size(), format, inlineArgs);
  va_end(inlineArgs);

  if (inlineResult < 0) {
    return {};
  }

  const std::size_t requiredSize = static_cast<std::size_t>(inlineResult);
  if (requiredSize < inlineBuffer.size()) {
    return std::string(inlineBuffer.data(), requiredSize);
  }

  std::vector<char> dynamicBuffer(requiredSize + 1);
  va_list dynamicArgs;
  va_copy(dynamicArgs, args);
  std::vsnprintf(dynamicBuffer.data(), dynamicBuffer.size(), format,
                 dynamicArgs);
  va_end(dynamicArgs);
  return std::string(dynamicBuffer.data(), requiredSize);
}

void SetLogCallback(PFN_CullingEngineLogCallback callback) {
  State().SetLogCallback(callback);
}

void SetStoreMessages(bool enabled) { State().SetStoreMessages(enabled); }

void AddLog(std::string_view message, const char* filename, int line) {
  State().AddLog(message, filename, line);
}

bool GetLog(char* msgOut, std::size_t capacity) {
  return State().GetLog(msgOut, capacity);
}

bool PrintLog() { return State().PrintLog(); }

void Log(Level level, const char* filename, int line,
         std::string_view message) {
  State().Log(level, filename, line, message);
}

void VLogFormatted(Level level, const char* filename, int line,
                   const char* format, va_list args) {
  Log(level, filename, line, FormatMessage(format, args));
}

void LogFormatted(Level level, const char* filename, int line,
                  const char* format, ...) {
  va_list args;
  va_start(args, format);
  VLogFormatted(level, filename, line, format, args);
  va_end(args);
}

}  // namespace CullingEngine
