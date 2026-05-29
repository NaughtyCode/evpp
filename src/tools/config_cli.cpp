// evpp-config-validate — standalone CLI tool for pre-deployment config validation.
// Usage: evpp-config-validate [--config-dir=PATH] [--verbose]
// Exit codes: 0=valid, 1=validation errors, 2=parse errors

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "runtime/config/config.h"
#include "runtime/config/config_constants.h"
#include "runtime/config/config_validator.h"

namespace {

int PrintUsage(const char* prog) {
	std::printf("Usage: %s [--config-dir=PATH] [--verbose]\n", prog);
	std::printf("Validate evpp config files without starting the server.\n\n");
	std::printf("Exit codes:\n");
	std::printf("  0 = all configs valid\n");
	std::printf("  1 = validation errors (semantic)\n");
	std::printf("  2 = parse errors (syntax)\n");
	return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
	std::string config_dir(engine::config::kConfigDir);
	bool verbose = false;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			return PrintUsage(argv[0]);
		} else if (arg.rfind("--config-dir=", 0) == 0) {
			config_dir = arg.substr(13);
		} else if (arg == "--verbose" || arg == "-v") {
			verbose = true;
		}
	}

	if (verbose) {
		std::printf("evpp-config-validate: checking %s\n", config_dir.c_str());
	}

	auto result = engine::ConfigManager::Instance().ValidateOnly(config_dir);

	if (!result.valid) {
		std::fprintf(stderr, "{\"status\":\"invalid\",\"errors\":\"%s\"}\n",
					 result.errors.c_str());
		// Check if errors contain "parse error" to distinguish exit codes
		if (result.errors.find("parse error") != std::string::npos) {
			return 2;
		}
		return 1;
	}

	if (!result.warnings.empty()) {
		std::printf("{\"status\":\"valid\",\"warnings\":\"%s\"}\n",
					result.warnings.c_str());
	} else if (verbose) {
		std::printf("{\"status\":\"valid\"}\n");
	}

	if (verbose) {
		std::printf("All configs valid.\n");
	}

	return 0;
}
