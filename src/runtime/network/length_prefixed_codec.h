#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/core/engine_api.h"

namespace evpp {
class Buffer;
}

namespace engine {

/* Length-prefixed message codec for TCP stream framing.
 *
 * Wire format: [4-byte big-endian length] [message body]
 *
 * On the wire, each message is prefixed with a 4-byte big-endian
 * unsigned integer indicating the payload length.  This allows the
 * receiver to split a TCP byte stream back into individual messages
 * regardless of how TCP segments (sticky packets) or fragments them.
 *
 * Max message size is configurable for DoS prevention — messages
 * exceeding the limit are discarded and the buffer is reset.
 *
 * Usage:
 *   LengthPrefixedCodec codec(64 * 1024);  // 64 KiB max
 *   std::string wire = codec.Encode("hello");  // [0,0,0,5] + "hello"
 *   auto msgs = codec.Decode(&buf);             // split buffer into messages
 */
class CLOUD_ENGINE_API LengthPrefixedCodec {
public:
	/* Construct a codec with the given max message size in bytes.
	 * @param max_message_size  Maximum allowed payload size (default 64 KiB).
	 *                          Set to 0 for unlimited (NOT recommended). */
	explicit LengthPrefixedCodec(uint32_t max_message_size = 64 * 1024);

	/* Encode a payload into a length-prefixed frame.
	 * @param payload  The message body to encode.
	 * @return  The wire-format frame: 4-byte BE length + payload.
	 *          Returns empty string if payload exceeds max_message_size. */
	std::string Encode(const std::string& payload);

	/* Encode a payload and append it directly to a Buffer.
	 * @param payload  The message body to encode.
	 * @param output   The buffer to append the framed message to. */
	void Encode(const std::string& payload, evpp::Buffer* output);

	/* Decode: extract all complete messages from a buffer.
	 *
	 * Reads the buffer looking for length-prefixed messages.  Complete
	 * messages are extracted and returned; incomplete data stays in the
	 * buffer for the next call.  If a message exceeds max_message_size,
	 * the entire buffer is discarded (malformed/malicious data).
	 *
	 * @param buffer  The input buffer to decode from.
	 * @return  Complete messages extracted from the buffer. */
	std::vector<std::string> Decode(evpp::Buffer* buffer);

	/* Set the maximum allowed message size.
	 * @param max_size  Maximum payload size in bytes. 0 = unlimited. */
	void SetMaxMessageSize(uint32_t max_size);

	/* Get the current maximum message size. */
	uint32_t GetMaxMessageSize() const;

private:
	uint32_t max_message_size_;

	/* Fixed header size: 4 bytes for the big-endian uint32 length prefix. */
	static constexpr size_t kHeaderSize = 4;
};

}  // namespace engine
