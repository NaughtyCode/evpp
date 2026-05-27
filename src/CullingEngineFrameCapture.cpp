#include <cerrno>

#include "CullingEngineFrameInfo.h"
#include "CullingEngineLog.h"

#if defined(CULLING_ENGINE_PLATFORM_ANDROID)
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>
#elif defined(CULLING_ENGINE_NATIVE)
#include <fstream>
#endif

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
#include <direct.h>
#pragma warning(disable : 4996)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

CullingEngineFrameCapture CullingEngineFrameCapture::Singleton;

void CullingEngineFrameCapture::SetLatestCaptureDepthMapFilename(
    const std::string& latestCaptureDepthMapFilename) {
  std::lock_guard<std::mutex> lock(mMutex);
  mLatestCaptureDepthMapFilename.assign(latestCaptureDepthMapFilename);
}

void CullingEngineFrameCapture::SetLatestCapFilename(
    const std::string& latestCapFilename) {
  std::lock_guard<std::mutex> lock(mMutex);
  mLatestCapFilename.assign(latestCapFilename);
}

void CullingEngineFrameCapture::SetLatestCapFilenameReady(
    const std::string& latestCapFilename) {
  std::lock_guard<std::mutex> lock(mMutex);
  mLatestCapFilenameReady.assign(latestCapFilename);
}

void CullingEngineFrameCapture::UpdateLatestCapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  if (mLatestCapFilenameReady.size() == 0) {
    return;
  }

  mLatestCapFilename.assign(mLatestCapFilenameReady);
  mLatestCapFilenameReady.clear();
}

std::string CullingEngineFrameCapture::GetLatestCaptureDepthMapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  return mLatestCaptureDepthMapFilename;
}

std::string CullingEngineFrameCapture::GetLatestCapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  return mLatestCapFilename;
}

void CullingEngineFrameCapture::ResetLatestCaptureDepthMapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  mLatestCaptureDepthMapFilename.clear();
}

void CullingEngineFrameCapture::ResetLatestCapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  mLatestCapFilename.clear();
}

bool CullingEngineFrameCapture::HasLatestCaptureDepthMapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  return !mLatestCaptureDepthMapFilename.empty();
}

bool CullingEngineFrameCapture::HasLatestCapFilename() {
  std::lock_guard<std::mutex> lock(mMutex);
  return !mLatestCapFilename.empty();
}

std::string CullingEngineFrameCapture::GetCaptureOutputPath() {
  std::lock_guard<std::mutex> lock(mMutex);
  return OutputDir;
}

bool CullingEngineFrameCapture::IsPathValid(const std::string& pathname) {
  struct stat info;
  if (stat(pathname.c_str(), &info) != 0) {
    return false;
  }
  if (info.st_mode & S_IFDIR) {
    return true;
  }

  return false;
}

bool CullingEngineFrameCapture::CreateDirectorySingle(
    const std::string& pathname) {
  if (pathname.empty()) {
    return false;
  }

#if defined(CULLING_ENGINE_PLATFORM_WINDOWS)
  return _mkdir(pathname.c_str()) == 0 || errno == EEXIST;
#else
  return mkdir(pathname.c_str(), 0777) == 0 || errno == EEXIST;
#endif
}

bool CullingEngineFrameCapture::CreateDirectories(const std::string& pathname) {
  if (pathname.empty()) {
    return false;
  }
  if (IsPathValid(pathname)) {
    return true;
  }

  for (size_t pos = 0; pos != std::string::npos;) {
    pos = pathname.find_first_of("/\\", pos + 1);
    std::string current = pathname.substr(0, pos);
    if (current.empty() || current == "/" || current == "\\" ||
        (current.size() == 2 && current[1] == ':')) {
      continue;
    }
    if (!IsPathValid(current) && !CreateDirectorySingle(current)) {
      return false;
    }
  }

  return IsPathValid(pathname);
}

std::string CullingEngineFrameCapture::GetOutputDirectory() {
  std::string output =
      CullingEngineFrameCapture::Singleton.GetCaptureOutputPath();
  bool pathValid = true;
#if defined(CULLING_ENGINE_PLATFORM_ANDROID)
  if (output.length() == 0) {
    output = "/sdcard/SOC/";
  }
  pathValid = CullingEngineFrameCapture::CreateDirectories(output);
#elif defined(CULLING_ENGINE_PLATFORM_WINDOWS)
  if (output == "") {
    return "D:/";
  }
  pathValid = CullingEngineFrameCapture::CreateDirectories(output);
#elif defined(CULLING_ENGINE_PLATFORM_MACOS) || \
    defined(CULLING_ENGINE_PLATFORM_LINUX)
  pathValid = CullingEngineFrameCapture::CreateDirectories(output);
#endif
  if (pathValid == false) {
    return "";
  }

  return output;
}

void CullingEngineFrameCapture::SetCaptureOutputPath(std::string output) {
  {
    std::lock_guard<std::mutex> lock(mMutex);
    this->OutputDir = output;
  }
  CullingEngine::AddLog(output, __FILE__, __LINE__);
}

const char* CullingEngineGetLatestCaptureDepthMapFilename() {
  static thread_local std::string latest;
  latest =
      CullingEngineFrameCapture::Singleton.GetLatestCaptureDepthMapFilename();
  if (!latest.empty()) {
    return latest.c_str();
  }

  return nullptr;
}

void CullingEngineResetLatestCaptureDepthMapFilename() {
  CullingEngineFrameCapture::Singleton.ResetLatestCaptureDepthMapFilename();
}

bool CullingEngineIsValidLatestCaptureDepthMapFilename() {
  return CullingEngineFrameCapture::Singleton
      .HasLatestCaptureDepthMapFilename();
}

const char* CullingEngineGetLatestCapFilename() {
  static thread_local std::string latest;
  latest = CullingEngineFrameCapture::Singleton.GetLatestCapFilename();
  if (!latest.empty()) {
    return latest.c_str();
  }

  return nullptr;
}

void CullingEngineResetLatestCapFilename() {
  CullingEngineFrameCapture::Singleton.ResetLatestCapFilename();
}

bool CullingEngineIsValidLatestCapFilename() {
  return CullingEngineFrameCapture::Singleton.HasLatestCapFilename();
}
