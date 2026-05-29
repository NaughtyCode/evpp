#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <cstring>
#include <string>

#include <runtime/evpp/buffer.h>

// ═══════════════════════════════════════════════════════════════════════════
// Buffer: basic construction & lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffer default construction", "[buffer][basic]") {
    evpp::Buffer buf;
    REQUIRE(buf.length() == 0);
    REQUIRE(buf.size() == 0);
    REQUIRE(buf.capacity() > 0);
    REQUIRE(buf.WritableBytes() > 0);
    REQUIRE(buf.PrependableBytes() > 0);
}

TEST_CASE("Buffer sized construction", "[buffer][basic]") {
    evpp::Buffer buf(1024, 16);
    REQUIRE(buf.WritableBytes() == 1024);
    REQUIRE(buf.PrependableBytes() == 16);
}

TEST_CASE("Buffer move construction", "[buffer][basic]") {
    evpp::Buffer a(256);
    a.Append("data", 4);
    REQUIRE(a.length() == 4);

    evpp::Buffer b(std::move(a));
    REQUIRE(b.length() == 4);
    REQUIRE(a.length() == 0);  // moved-from is empty
    REQUIRE(a.capacity() == 0);
}

TEST_CASE("Buffer move assignment", "[buffer][basic]") {
    evpp::Buffer a(256);
    a.Append("hello", 5);
    evpp::Buffer b(128);

    b = std::move(a);
    REQUIRE(b.length() == 5);
    REQUIRE(b.ToString() == "hello");
    REQUIRE(a.capacity() == 0);
}

TEST_CASE("Buffer Swap", "[buffer][basic]") {
    evpp::Buffer a(256);
    evpp::Buffer b(512);
    a.Append("aaaa", 4);
    b.Append("bbbb", 4);

    a.Swap(b);
    REQUIRE(a.ToString() == "bbbb");
    REQUIRE(b.ToString() == "aaaa");
}

// ═══════════════════════════════════════════════════════════════════════════
// Buffer: write / read roundtrip
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffer Write and Read", "[buffer][rw]") {
    evpp::Buffer buf;
    const char* input = "Hello, Buffer!";
    buf.Write(input, strlen(input));
    REQUIRE(buf.length() == strlen(input));

    std::string result = buf.ToString();
    REQUIRE(result == input);
}

TEST_CASE("Buffer Append string", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.Append("hello");
    REQUIRE(buf.length() == 5);
    REQUIRE(buf.ToString() == "hello");
}

TEST_CASE("Buffer Append multiple", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.Append("abc");
    buf.Append("def");
    buf.Append("ghi");
    REQUIRE(buf.length() == 9);
    REQUIRE(buf.ToString() == "abcdefghi");
}

TEST_CASE("Buffer Prepend and Read", "[buffer][rw]") {
    evpp::Buffer buf(256, 16);
    buf.Append("world");
    buf.Prepend("hello ", 6);
    REQUIRE(buf.length() == 11);
    REQUIRE(buf.ToString() == "hello world");
}

TEST_CASE("Buffer Skip partial", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.Append("abcdef");
    buf.Skip(2);
    REQUIRE(buf.length() == 4);
    REQUIRE(buf.ToString() == "cdef");
}

TEST_CASE("Buffer Skip all", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.Append("abcdef");
    buf.Skip(6);
    REQUIRE(buf.length() == 0);
}

TEST_CASE("Buffer ReadInt roundtrip", "[buffer][rw]") {
    evpp::Buffer buf;

    buf.AppendInt32(42);
    REQUIRE(buf.length() == 4);
    REQUIRE(buf.ReadInt32() == 42);
    REQUIRE(buf.length() == 0);

    buf.AppendInt64(123456789012345);
    REQUIRE(buf.ReadInt64() == 123456789012345);

    buf.AppendInt16(32767);
    REQUIRE(buf.ReadInt16() == 32767);

    buf.AppendInt8(127);
    REQUIRE(buf.ReadInt8() == 127);
}

TEST_CASE("Buffer Peek does not consume", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.AppendInt32(99);
    REQUIRE(buf.PeekInt32() == 99);
    REQUIRE(buf.length() == 4);  // still there
    REQUIRE(buf.ReadInt32() == 99);
    REQUIRE(buf.length() == 0);
}

TEST_CASE("Buffer Read on short buffer returns 0", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.AppendInt8(1);
    REQUIRE(buf.ReadInt32() == 0);   // not enough data
    REQUIRE(buf.PeekInt64() == 0);   // not enough data
}

TEST_CASE("Buffer large write triggers grow", "[buffer][rw]") {
    evpp::Buffer buf(64);  // small initial
    std::string big(4096, 'X');
    buf.Append(big);
    REQUIRE(buf.length() == 4096);
    REQUIRE(buf.ToString() == big);
}

TEST_CASE("Buffer Next and NextAll", "[buffer][rw]") {
    evpp::Buffer buf;
    buf.Append("abcdefgh");

    auto s1 = buf.Next(3);
    REQUIRE(s1.ToString() == "abc");
    REQUIRE(buf.length() == 5);

    auto s2 = buf.NextAll();
    REQUIRE(s2.ToString() == "defgh");
    REQUIRE(buf.length() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Buffer: Reset / Truncate / Reserve / Shrink
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffer Reset", "[buffer][mutate]") {
    evpp::Buffer buf;
    buf.Append("abcdef");
    buf.Skip(2);
    buf.Reset();
    REQUIRE(buf.length() == 0);
    // Buffer should still be writable after reset
    buf.Append("new");
    REQUIRE(buf.ToString() == "new");
}

TEST_CASE("Buffer Truncate", "[buffer][mutate]") {
    evpp::Buffer buf;
    buf.Append("abcdefgh");
    buf.Truncate(4);
    REQUIRE(buf.length() == 4);
    REQUIRE(buf.ToString() == "abcd");

    buf.Truncate(0);
    REQUIRE(buf.length() == 0);
}

TEST_CASE("Buffer Reserve grows capacity", "[buffer][mutate]") {
    evpp::Buffer buf(64);
    size_t cap_before = buf.capacity();
    buf.Reserve(4096);
    REQUIRE(buf.capacity() >= 4096);
}

TEST_CASE("Buffer max capacity can be disabled", "[buffer][mutate]") {
    evpp::Buffer buf;
    buf.SetMaxCapacity(0);
    buf.Append("data", 4);
    REQUIRE_FALSE(buf.AtMaxCapacity());
    REQUIRE(buf.GetMaxCapacity() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Buffer: search helpers
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffer FindCRLF", "[buffer][search]") {
    evpp::Buffer buf;
    buf.Append("line1\r\nline2");
    const char* crlf = buf.FindCRLF();
    REQUIRE(crlf != nullptr);
    REQUIRE(*crlf == '\r');
}

TEST_CASE("Buffer FindCRLF not found", "[buffer][search]") {
    evpp::Buffer buf;
    buf.Append("no line ending here");
    const char* crlf = buf.FindCRLF();
    REQUIRE(crlf == nullptr);
}

TEST_CASE("Buffer FindEOL", "[buffer][search]") {
    evpp::Buffer buf;
    buf.Append("hello\nworld");
    const char* eol = buf.FindEOL();
    REQUIRE(eol != nullptr);
    REQUIRE(*eol == '\n');
}

TEST_CASE("Buffer ToText null-termination", "[buffer][text]") {
    evpp::Buffer buf;
    buf.Append("hello");
    buf.ToText();
    REQUIRE(buf.length() == 5);  // length unchanged (null appended then unwritten)
}

// ═══════════════════════════════════════════════════════════════════════════
// Buffer: boundary values
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffer zero-length write", "[buffer][edge]") {
    evpp::Buffer buf;
    buf.Write("", 0);
    REQUIRE(buf.length() == 0);
}

TEST_CASE("Buffer single byte", "[buffer][edge]") {
    evpp::Buffer buf;
    buf.AppendInt8(0x7F);
    REQUIRE(buf.length() == 1);
    REQUIRE(buf.ReadInt8() == 0x7F);
}

TEST_CASE("Buffer int32 boundary values", "[buffer][edge]") {
    evpp::Buffer buf;

    buf.AppendInt32(0);
    REQUIRE(buf.ReadInt32() == 0);

    buf.AppendInt32(INT32_MAX);
    REQUIRE(buf.ReadInt32() == INT32_MAX);

    buf.AppendInt32(INT32_MIN);
    REQUIRE(buf.ReadInt32() == INT32_MIN);
}

TEST_CASE("Buffer int64 boundary values", "[buffer][edge]") {
    evpp::Buffer buf;

    buf.AppendInt64(0);
    REQUIRE(buf.ReadInt64() == 0);

    buf.AppendInt64(INT64_MAX);
    REQUIRE(buf.ReadInt64() == INT64_MAX);

    buf.AppendInt64(INT64_MIN);
    REQUIRE(buf.ReadInt64() == INT64_MIN);
}

TEST_CASE("Buffer read empty returns 0", "[buffer][edge]") {
    evpp::Buffer buf;
    REQUIRE(buf.PeekInt8() == 0);
    REQUIRE(buf.PeekInt16() == 0);
    REQUIRE(buf.PeekInt32() == 0);
    REQUIRE(buf.PeekInt64() == 0);
    REQUIRE(buf.ReadByte() == '\0');
}
