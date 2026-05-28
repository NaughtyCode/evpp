#include "runtime/network/length_prefixed_codec.h"

#include <cstring>

#include "runtime/evpp/buffer.h"
#include "runtime/profiler/profiler_events.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace engine {

LengthPrefixedCodec::LengthPrefixedCodec(uint32_t max_message_size)
	: max_message_size_(max_message_size) {
}

std::string LengthPrefixedCodec::Encode(const std::string& payload) {
	ENGINE_PROFILE_SCOPE("engine.script", "NetEncode");
	if (max_message_size_ > 0 && payload.size() > max_message_size_) {
		return {};
	}

	std::string result;
	result.reserve(kHeaderSize + payload.size());

	uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
	result.append(reinterpret_cast<const char*>(&net_len), kHeaderSize);
	result.append(payload);

	return result;
}

void LengthPrefixedCodec::Encode(const std::string& payload, evpp::Buffer* output) {
	ENGINE_PROFILE_SCOPE("engine.script", "NetEncodeBuf");
	if (max_message_size_ > 0 && payload.size() > max_message_size_) {
		return;
	}

	uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
	output->Append(&net_len, kHeaderSize);
	output->Append(payload.data(), payload.size());
}

std::vector<std::string> LengthPrefixedCodec::Decode(evpp::Buffer* buffer) {
	ENGINE_PROFILE_SCOPE("engine.script", "NetDecode");
	std::vector<std::string> messages;
	size_t readable = buffer->length();

	while (readable >= kHeaderSize) {
		/* Peek the 4-byte length header without consuming */
		uint32_t msg_len_net = 0;
		std::memcpy(&msg_len_net, buffer->data(), kHeaderSize);
		uint32_t msg_len = ntohl(msg_len_net);

		if (max_message_size_ > 0 && msg_len > max_message_size_) {
			/* Malformed or malicious — discard entire buffer */
			buffer->Reset();
			break;
		}

		if (readable < kHeaderSize + msg_len) {
			/* Incomplete message — wait for more data */
			break;
		}

		/* Extract complete message */
		buffer->Skip(kHeaderSize);  /* consume length header */
		messages.emplace_back(buffer->data(), msg_len);
		buffer->Skip(msg_len);  /* consume message body */
		readable = buffer->length();
	}

	return messages;
}

void LengthPrefixedCodec::SetMaxMessageSize(uint32_t max_size) {
	max_message_size_ = max_size;
}

uint32_t LengthPrefixedCodec::GetMaxMessageSize() const {
	return max_message_size_;
}

}  // namespace engine
