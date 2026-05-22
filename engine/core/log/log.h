#pragma once

#include <string>

#include <quill/Logger.h>

namespace engine {

quill::Logger* GetLogger(const std::string& name = "root");

void InitLogger(const std::string& log_dir);

void ShutdownLogger();

} // namespace engine
