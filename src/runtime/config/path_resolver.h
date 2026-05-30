#pragma once

#include <filesystem>

namespace engine::config {

inline std::filesystem::path ResolvePathFromWorkingTree(const std::filesystem::path& path) {
	if (path.empty()) {
		return path;
	}

	if (path.is_absolute()) {
		return path.lexically_normal();
	}

	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		return path.lexically_normal();
	}

	auto current = std::filesystem::current_path(ec);
	if (ec) {
		return path.lexically_normal();
	}

	for (;;) {
		auto candidate = (current / path).lexically_normal();
		std::error_code exists_ec;
		if (std::filesystem::exists(candidate, exists_ec)) {
			return candidate;
		}

		auto parent = current.parent_path();
		if (parent.empty() || parent == current) {
			break;
		}
		current = std::move(parent);
	}

	return path.lexically_normal();
}

}  // namespace engine::config
