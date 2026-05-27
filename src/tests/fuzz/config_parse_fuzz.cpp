// Fuzz target: ConfigManager::LoadRuntimeFromString
// Feeds arbitrary bytes to the JSON config parser to find crashes,
// buffer overflows, or undefined behavior in glaze JSON parsing.
//
// Build (libFuzzer, Linux/clang):
//   clang++ -g -fsanitize=fuzzer,address config_parse_fuzz.cpp -o fuzz_config
//
// Build (standalone, any platform):
//   cl /EHsc /std:c++17 config_parse_fuzz.cpp /Fe:fuzz_config.exe
//   fuzz_config.exe < input.bin

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "runtime/config/config.h"

namespace {

void FuzzEntry(const uint8_t* data, size_t size) {
	using engine::ConfigManager;

	std::string input(reinterpret_cast<const char*>(data), size);

	// Test LoadRuntimeFromString — the most commonly used config parser.
	// We don't care about the return value; we just want to ensure no crash.
	ConfigManager::Instance().LoadRuntimeFromString(input);

	// Also try LoadClientFromString and LoadServerFromString.
	ConfigManager::Instance().LoadClientFromString(input);
	ConfigManager::Instance().LoadServerFromString(input);
}

}  // namespace

// ── libFuzzer entry point ───────────────────────────────────────────────
#ifdef __AFL_COMPILER
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	FuzzEntry(data, size);
	return 0;
}
#endif

// ── Standalone driver ───────────────────────────────────────────────────
#ifndef __AFL_COMPILER
int main() {
	std::vector<uint8_t> input;
	input.assign(std::istreambuf_iterator<char>(std::cin),
				 std::istreambuf_iterator<char>());
	if (input.empty()) {
		// Generate some edge cases if no input provided.
		std::fprintf(stderr, "fuzz: no input, testing edge cases\n");
		const char* cases[] = {
			"", "{}", "[]", "null", "42", "\"string\"",
			"{\"resource_dir\":\"test\"}",
			"{\"log\":{\"dir\":\"/tmp\"}}",
			std::string(65536, '{').c_str(),  // deeply nested
		};
		for (auto* c : cases) {
			FuzzEntry(reinterpret_cast<const uint8_t*>(c), strlen(c));
		}
	} else {
		FuzzEntry(input.data(), input.size());
	}
	std::fprintf(stderr, "fuzz: processed %zu bytes, no crash\n", input.size());
	return 0;
}
#endif
