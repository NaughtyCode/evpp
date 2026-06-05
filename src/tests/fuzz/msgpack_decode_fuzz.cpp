// Fuzz target: Lua msgpack.decode
// Feeds arbitrary bytes to the Lua MessagePack decoder to find crashes,
// buffer overflows, or sandbox escapes.
//
// This target requires the full engine (Lua VM + msgpack binding).
//
// Build (libFuzzer, Linux/clang):
//   clang++ -g -fsanitize=fuzzer,address msgpack_decode_fuzz.cpp -o fuzz_msgpack
//
// Build (standalone):
//   fuzz_msgpack.exe < input.bin

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "runtime/script/msgpack_bind.h"
#include "runtime/vm/vm.h"

namespace {

void FuzzEntry(const uint8_t* data, size_t size) {
	using engine::ScriptVM;

	// Create a sandboxed VM for safety (Strict level blocks dangerous APIs).
	ScriptVM vm(LuaSandboxLevel::Strict);
	engine::script::ExportMsgPack(vm);

	// Push the raw bytes as a Lua string, then try to decode it.
	// Use pcall to catch decoder errors; the fuzz target must not crash.
	std::string lua_code;
	lua_code += "local ok, err = pcall(cmsgpack.unpack, input_bytes)\n";

	// Push the input as a Lua string global.
	lua_State* L = vm.GetState();
	lua_pushlstring(L, reinterpret_cast<const char*>(data), size);
	lua_setglobal(L, "input_bytes");

	// Execute the decode attempt — must not crash.
	vm.DoString(lua_code);
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
		const uint8_t empty[] = {};
		const uint8_t incomplete[] = {0x81, 0xa3};  // fixmap with truncated key
		const uint8_t deep_nest[] = {
			0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91,  // 8-deep arrays
		};
		FuzzEntry(empty, 0);
		FuzzEntry(incomplete, sizeof(incomplete));
		FuzzEntry(deep_nest, sizeof(deep_nest));
	} else {
		FuzzEntry(input.data(), input.size());
	}
	std::fprintf(stderr, "fuzz: processed %zu bytes, no crash\n", input.size());
	return 0;
}
#endif
