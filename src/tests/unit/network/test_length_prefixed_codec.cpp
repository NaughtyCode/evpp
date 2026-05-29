#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <runtime/evpp/buffer.h>
#include <runtime/network/length_prefixed_codec.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * LengthPrefixedCodec — encode/decode round-trip tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("LengthPrefixedCodec encode/decode round-trip", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	std::string wire = codec.Encode("hello");
	buf.Append(wire.data(), wire.size());

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "hello");
	REQUIRE(buf.length() == 0);  /* fully consumed */
}

TEST_CASE("LengthPrefixedCodec decode sticky packets", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	/* Encode two messages and append them to the same buffer (simulating TCP coalescing) */
	std::string w1 = codec.Encode("first");
	std::string w2 = codec.Encode("second");
	buf.Append(w1.data(), w1.size());
	buf.Append(w2.data(), w2.size());

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 2);
	REQUIRE(messages[0] == "first");
	REQUIRE(messages[1] == "second");
	REQUIRE(buf.length() == 0);
}

TEST_CASE("LengthPrefixedCodec decode fragmented message", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	std::string wire = codec.Encode("fragmented");

	/* Send only the first 2 bytes of the 4-byte header -> incomplete */
	buf.Append(wire.data(), 2);
	auto messages1 = codec.Decode(&buf);
	REQUIRE(messages1.empty());
	REQUIRE(buf.length() == 2);  /* data stays in buffer */

	/* Send the rest -> complete message */
	buf.Append(wire.data() + 2, wire.size() - 2);
	auto messages2 = codec.Decode(&buf);
	REQUIRE(messages2.size() == 1);
	REQUIRE(messages2[0] == "fragmented");
	REQUIRE(buf.length() == 0);
}

TEST_CASE("LengthPrefixedCodec partial message body", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	std::string wire = codec.Encode("partial_body_test");

	/* Send header + only half of the body */
	size_t half = 4 + (wire.size() - 4) / 2;
	buf.Append(wire.data(), half);
	auto messages1 = codec.Decode(&buf);
	REQUIRE(messages1.empty());
	REQUIRE(buf.length() == half);  /* data stays in buffer */

	/* Send remaining body -> complete */
	buf.Append(wire.data() + half, wire.size() - half);
	auto messages2 = codec.Decode(&buf);
	REQUIRE(messages2.size() == 1);
	REQUIRE(messages2[0] == "partial_body_test");
	REQUIRE(buf.length() == 0);
}

TEST_CASE("LengthPrefixedCodec max message size enforcement", "[network][codec]") {
	engine::LengthPrefixedCodec codec(100);  /* 100 byte max */
	evpp::Buffer buf;

	/* Encode a message under the limit */
	std::string ok = codec.Encode(std::string(50, 'x'));
	REQUIRE(!ok.empty());
	REQUIRE(ok.size() == 50 + 4);

	/* Encode a message over the limit */
	std::string over = codec.Encode(std::string(200, 'x'));
	REQUIRE(over.empty());

	/* Decode: craft a message with a large declared size (DoS simulation).
	 * Wire format is 4-byte big-endian length.  0x0000FFFF big-endian = 65535. */
	uint8_t big_header[4] = {0x00, 0x00, 0xFF, 0xFF};
	buf.Append(big_header, 4);
	buf.Append(std::string(65535, 'y').data(), 65535);

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.empty());
	REQUIRE(buf.length() == 0);  /* buffer reset on oversized message */
}

TEST_CASE("LengthPrefixedCodec empty message", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	std::string wire = codec.Encode("");
	REQUIRE(wire.size() == 4);  /* just the header, length=0 */

	buf.Append(wire.data(), wire.size());
	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0].empty());
}

TEST_CASE("LengthPrefixedCodec max message size unlimited", "[network][codec]") {
	engine::LengthPrefixedCodec codec(0);  /* unlimited */
	evpp::Buffer buf;

	/* Large but reasonable message should pass */
	std::string payload(100 * 1024, 'z');  /* 100 KiB */
	std::string wire = codec.Encode(payload);
	REQUIRE(!wire.empty());
	REQUIRE(wire.size() == payload.size() + 4);

	buf.Append(wire.data(), wire.size());
	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == payload);
}

TEST_CASE("LengthPrefixedCodec set max message size", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	REQUIRE(codec.GetMaxMessageSize() == 64 * 1024);

	codec.SetMaxMessageSize(128);
	REQUIRE(codec.GetMaxMessageSize() == 128);

	/* Encode should now reject a message > 128 bytes */
	std::string over = codec.Encode(std::string(200, 'x'));
	REQUIRE(over.empty());
}

TEST_CASE("LengthPrefixedCodec encode to Buffer output", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	codec.Encode("test", &buf);
	REQUIRE(buf.length() == 4 + 4);  /* header + "test" */

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "test");
}

TEST_CASE("LengthPrefixedCodec handles null buffers", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);

	codec.Encode("ignored", nullptr);

	auto messages = codec.Decode(nullptr);
	REQUIRE(messages.empty());
}

TEST_CASE("LengthPrefixedCodec trailing incomplete header", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	/* Append only 2 bytes (incomplete 4-byte header) */
	char partial[2] = {0x00, 0x01};
	buf.Append(partial, 2);

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.empty());
	REQUIRE(buf.length() == 2);  /* data stays for next read */
}

TEST_CASE("LengthPrefixedCodec exactly complete message", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	std::string wire = codec.Encode("exact_boundary");
	/* Append exactly one complete message — nothing more */
	buf.Append(wire.data(), wire.size());

	auto messages = codec.Decode(&buf);
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "exact_boundary");
	REQUIRE(buf.length() == 0);
}

TEST_CASE("LengthPrefixedCodec multiple decode calls with interleaved writes", "[network][codec]") {
	engine::LengthPrefixedCodec codec(64 * 1024);
	evpp::Buffer buf;

	/* Simulate real-world pattern: write a bit, decode, write more, decode */
	std::string w1 = codec.Encode("msg1");
	buf.Append(w1.data(), 2);  /* partial header */
	auto r1 = codec.Decode(&buf);
	REQUIRE(r1.empty());

	buf.Append(w1.data() + 2, w1.size() - 2);  /* rest of msg1 */
	auto r2 = codec.Decode(&buf);
	REQUIRE(r2.size() == 1);
	REQUIRE(r2[0] == "msg1");

	std::string w2 = codec.Encode("msg2");
	std::string w3 = codec.Encode("msg3");
	buf.Append(w2.data(), w2.size());
	buf.Append(w3.data(), w3.size());
	auto r3 = codec.Decode(&buf);
	REQUIRE(r3.size() == 2);
	REQUIRE(r3[0] == "msg2");
	REQUIRE(r3[1] == "msg3");
}
