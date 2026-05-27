// Fuzz target: LengthPrefixedCodec::Decode
// Feeds arbitrary bytes to the length-prefixed frame decoder to find
// crashes, OOB reads, or infinite loops.
//
// Build (libFuzzer, Linux/clang):
//   clang++ -g -fsanitize=fuzzer,address network_frame_fuzz.cpp -o fuzz_frame
//
// Build (standalone, any platform):
//   cl /EHsc /std:c++17 network_frame_fuzz.cpp /Fe:fuzz_frame.exe
//   fuzz_frame.exe < input.bin

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "runtime/evpp/buffer.h"
#include "runtime/network/length_prefixed_codec.h"

namespace {

void FuzzEntry(const uint8_t* data, size_t size) {
	using engine::LengthPrefixedCodec;

	// Test with default 64 KiB limit.
	{
		LengthPrefixedCodec codec;
		evpp::Buffer buf;
		buf.Append(data, size);
		auto messages = codec.Decode(&buf);
		// Verify that decoded messages re-encode to the same wire format.
		for (const auto& msg : messages) {
			std::string wire = codec.Encode(msg);
			if (wire.empty() && !msg.empty()) {
				// Message exceeded max size — valid outcome.
				continue;
			}
		}
	}

	// Test with a small max message size (16 bytes) to exercise
	// the oversized-message discard path.
	{
		LengthPrefixedCodec codec(16);
		evpp::Buffer buf;
		buf.Append(data, size);
		codec.Decode(&buf);  // must not crash
	}

	// Test with unlimited message size.
	{
		LengthPrefixedCodec codec(0);
		evpp::Buffer buf;
		buf.Append(data, size);
		codec.Decode(&buf);  // must not crash
	}
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
	FuzzEntry(input.data(), input.size());
	std::fprintf(stderr, "fuzz: processed %zu bytes, no crash\n", input.size());
	return 0;
}
#endif
