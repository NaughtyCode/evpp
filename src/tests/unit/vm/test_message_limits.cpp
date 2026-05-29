#include <catch2/catch_test_macros.hpp>

#include "log_init.h"
#include "runtime/config/limits.h"
#include "runtime/evpp/buffer.h"
#include "runtime/network/length_prefixed_codec.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * ResourceLimits constants
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("ResourceLimits defaults are non-zero", "[limits][constants]") {
    REQUIRE(engine::config::kDefaultMaxMessageSize > 0);
    REQUIRE(engine::config::kDefaultMaxBufferCapacity > 0);
    REQUIRE(engine::config::kDefaultMaxHttpBodySize > 0);
    REQUIRE(engine::config::kDefaultMaxMsgpackDepth > 0);
}

TEST_CASE("Max message size is at least 1KB", "[limits][constants]") {
    REQUIRE(engine::config::kDefaultMaxMessageSize >= 1024);
}

TEST_CASE("Buffer capacity is larger than message size", "[limits][constants]") {
    REQUIRE(engine::config::kDefaultMaxBufferCapacity >=
            engine::config::kDefaultMaxMessageSize);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Buffer max capacity
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Buffer has default max capacity", "[limits][buffer]") {
    evpp::Buffer buf;
    REQUIRE(buf.GetMaxCapacity() > 0);
    REQUIRE(buf.GetMaxCapacity() == 256 * 1024);
}

TEST_CASE("Buffer AtMaxCapacity returns false when under limit", "[limits][buffer]") {
    evpp::Buffer buf;
    REQUIRE_FALSE(buf.AtMaxCapacity());
}

TEST_CASE("Buffer AtMaxCapacity returns true when at limit", "[limits][buffer]") {
    evpp::Buffer buf(/*initial_size*/ 256 * 1024);
    /* Fill buffer to max */
    std::string data(256 * 1024, 'x');
    buf.Write(data.data(), data.size());
    REQUIRE(buf.AtMaxCapacity());
}

TEST_CASE("Buffer SetMaxCapacity changes the limit", "[limits][buffer]") {
    evpp::Buffer buf;
    buf.SetMaxCapacity(1024);
    REQUIRE(buf.GetMaxCapacity() == 1024);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LengthPrefixedCodec max message size
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_CASE("Codec default max matches ResourceLimits", "[limits][codec]") {
    engine::LengthPrefixedCodec codec;
    REQUIRE(codec.GetMaxMessageSize() ==
            engine::config::kDefaultMaxMessageSize);
}

TEST_CASE("Codec Encode rejects oversized payload", "[limits][codec]") {
    engine::LengthPrefixedCodec codec(1024);
    std::string big(2048, 'x');
    auto framed = codec.Encode(big);
    REQUIRE(framed.empty());
}

TEST_CASE("Codec Encode accepts payload at exactly max", "[limits][codec]") {
    engine::LengthPrefixedCodec codec(1024);
    std::string exact(1024, 'x');
    auto framed = codec.Encode(exact);
    REQUIRE_FALSE(framed.empty());
}

TEST_CASE("Codec Encode appending rejects oversized", "[limits][codec]") {
    engine::LengthPrefixedCodec codec(1024);
    evpp::Buffer buf;
    std::string big(2048, 'x');
    codec.Encode(big, &buf);
    REQUIRE(buf.length() == 0);
}
