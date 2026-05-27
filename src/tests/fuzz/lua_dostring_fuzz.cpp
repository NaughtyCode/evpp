// Fuzz target: Lua DoString under sandbox
// Feeds arbitrary strings to the Lua VM to find crashes, sandbox escapes,
// or undefined behavior in the Lua interpreter under our sandbox config.
//
// Build (libFuzzer, Linux/clang):
//   clang++ -g -fsanitize=fuzzer,address lua_dostring_fuzz.cpp -o fuzz_lua
//
// Build (standalone):
//   fuzz_lua.exe < input.bin

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "runtime/vm/vm.h"
#include "runtime/vm/sandbox.h"

namespace {

// Test all sandbox levels to ensure no level crashes on arbitrary input.
void TestWithSandbox(const std::string& input, LuaSandboxLevel level) {
	using engine::ScriptVM;
	ScriptVM vm(level);
	// DoString handles Lua errors gracefully — we just need no crash.
	vm.DoString(input);
}

void FuzzEntry(const uint8_t* data, size_t size) {
	std::string input(reinterpret_cast<const char*>(data), size);

	// Skip inputs with null bytes in the middle (Lua strings can't have
	// embedded nulls, but raw bytes might).
	std::string sanitized;
	for (size_t i = 0; i < size; ++i) {
		char c = static_cast<char>(data[i]);
		if (c == 0) continue;    // skip null bytes
		if (c == '\x1b') continue;  // skip ESC (can confuse terminals if printed)
		sanitized += c;
	}
	if (sanitized.empty()) return;

	TestWithSandbox(sanitized, LuaSandboxLevel::Strict);
	TestWithSandbox(sanitized, LuaSandboxLevel::Server);
	TestWithSandbox(sanitized, LuaSandboxLevel::Full);
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
		std::fprintf(stderr, "fuzz: no input, testing edge cases\n");
		const char* cases[] = {
			"",
			"return",
			"return 1+1",
			"function() end",
			"while true do end",  // would hang — sandbox should handle
			"os.execute('echo pwned')",  // sandbox should block
			std::string(10000, '(').c_str(),  // deeply nested
			std::string(10000, '-').c_str(),  // long comment
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
