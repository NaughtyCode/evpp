#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <typeinfo>

#include <runtime/evpp/any.h>
#include <runtime/evpp/duration.h>
#include <runtime/evpp/httpc/url_parser.h>
#include <runtime/evpp/slice.h>
#include <runtime/evpp/timestamp.h>

TEST_CASE("Duration converts between time units and timeval", "[evpp][duration]") {
	evpp::Duration d(1.5);
	REQUIRE(d.Nanoseconds() == 1500000000LL);
	REQUIRE(d.Seconds() == Catch::Approx(1.5));
	REQUIRE(d.Milliseconds() == Catch::Approx(1500.0));
	REQUIRE(d.Microseconds() == Catch::Approx(1500000.0));

	timeval tv = d.TimeVal();
	REQUIRE(tv.tv_sec == 1);
	REQUIRE(tv.tv_usec == 500000);

	evpp::Duration from_tv(tv);
	REQUIRE(from_tv == d);
	REQUIRE_FALSE(from_tv.IsZero());
}

TEST_CASE("Duration arithmetic and comparisons are value based", "[evpp][duration]") {
	evpp::Duration a(100);
	evpp::Duration b(50);

	REQUIRE(a > b);
	REQUIRE(b < a);
	REQUIRE(a >= b);
	REQUIRE(b <= a);

	a -= b;
	REQUIRE(a.Nanoseconds() == 50);
	a += evpp::Duration(25);
	REQUIRE(a.Nanoseconds() == 75);
	a *= 2;
	REQUIRE(a.Nanoseconds() == 150);
	a /= 3;
	REQUIRE(a.Nanoseconds() == 50);
}

TEST_CASE("Timestamp stores Unix time and supports duration arithmetic", "[evpp][timestamp]") {
	timeval tv{};
	tv.tv_sec = 12;
	tv.tv_usec = 345678;

	evpp::Timestamp ts(tv);
	REQUIRE(ts.Unix() == 12);
	REQUIRE(ts.UnixMicro() == 12345678LL);
	REQUIRE(ts.UnixNano() == 12345678000LL);
	REQUIRE_FALSE(ts.IsEpoch());

	auto later = ts + evpp::Duration(2 * evpp::Duration::kSecond);
	REQUIRE(later.Unix() == 14);
	REQUIRE((later - ts).Nanoseconds() == 2 * evpp::Duration::kSecond);

	later -= evpp::Duration(500 * evpp::Duration::kMillisecond);
	REQUIRE((later - ts).Milliseconds() == Catch::Approx(1500.0));
}

TEST_CASE("Slice references data and compares lexicographically", "[evpp][slice]") {
	std::string value = "abcdef";
	evpp::Slice slice(value);

	REQUIRE(slice.size() == 6);
	REQUIRE(slice[0] == 'a');
	REQUIRE(slice.ToString() == "abcdef");
	REQUIRE(slice.compare(evpp::Slice("abcdeg")) < 0);
	REQUIRE(slice < evpp::Slice("abcdeg"));
	REQUIRE(slice == evpp::Slice("abcdef"));

	slice.remove_prefix(2);
	REQUIRE(slice.ToString() == "cdef");
	slice.clear();
	REQUIRE(slice.empty());
}

TEST_CASE("Any stores, copies, swaps, and type-checks values", "[evpp][any]") {
	evpp::Any empty;
	REQUIRE(empty.IsEmpty());
	REQUIRE(empty.GetType() == typeid(void));
	REQUIRE(empty.Get<int>() == 0);

	evpp::Any value(std::string("payload"));
	REQUIRE_FALSE(value.IsEmpty());
	REQUIRE(value.GetType() == typeid(std::string));
	REQUIRE(value.Get<std::string>() == "payload");
	REQUIRE(value.Get<int>() == 0);

	evpp::Any copied(value);
	REQUIRE(copied.Get<std::string>() == "payload");

	evpp::Any number(42);
	value.swap(number);
	REQUIRE(value.Get<int>() == 42);
	REQUIRE(number.Get<std::string>() == "payload");

	auto ptr = std::make_shared<int>(7);
	evpp::Any shared(ptr);
	REQUIRE(evpp::any_cast<std::shared_ptr<int>>(shared).get() == ptr.get());
	REQUIRE(*evpp::any_cast<std::shared_ptr<int>>(shared) == 7);
}

TEST_CASE("URLParser parses schema, host, port, path, and query", "[evpp][httpc][url]") {
	evpp::httpc::URLParser parser("HTTP://Example.COM:8080/path/to?q=1&name=evpp#frag");

	REQUIRE(parser.schema == "http");
	REQUIRE(parser.host == "example.com");
	REQUIRE(parser.port == 8080);
	REQUIRE(parser.path == "/path/to");
	REQUIRE(parser.query == "q=1&name=evpp");
}

TEST_CASE("URLParser accepts host-only and query-only URLs", "[evpp][httpc][url]") {
	evpp::httpc::URLParser host_only("Example.COM");
	REQUIRE(host_only.schema.empty());
	REQUIRE(host_only.host == "example.com");
	REQUIRE(host_only.port == 80);
	REQUIRE(host_only.path.empty());
	REQUIRE(host_only.query.empty());

	evpp::httpc::URLParser query_only("example.com?x=1");
	REQUIRE(query_only.host == "example.com");
	REQUIRE(query_only.path.empty());
	REQUIRE(query_only.query == "x=1");
}

TEST_CASE("URLParser keeps default port on invalid or out-of-range port", "[evpp][httpc][url]") {
	evpp::httpc::URLParser bad_text("http://example.com:abc/path");
	REQUIRE(bad_text.host == "example.com");
	REQUIRE(bad_text.port == 80);
	REQUIRE(bad_text.path == "/path");

	evpp::httpc::URLParser bad_range("http://example.com:70000/path");
	REQUIRE(bad_range.host == "example.com");
	REQUIRE(bad_range.port == 80);
	REQUIRE(bad_range.path == "/path");
}
