#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace engine {

constexpr uint64_t kMaxLuaScriptFileBytes = 16ULL * 1024ULL * 1024ULL;
constexpr const char* kLuaTextChunkMode = "t";

inline bool ValidateLuaScriptFileForLoad(const std::string& filepath,
										 std::string& error) {
	std::error_code ec;
	auto status = std::filesystem::status(filepath, ec);
	if (ec || !std::filesystem::is_regular_file(status)) {
		error = ec ? ec.message() : "file not found";
		return false;
	}

	auto size = std::filesystem::file_size(filepath, ec);
	if (ec) {
		error = "failed to determine script file size: " + ec.message();
		return false;
	}
	if (size > kMaxLuaScriptFileBytes) {
		error = "script file exceeds maximum size";
		return false;
	}
	return true;
}

}  // namespace engine
