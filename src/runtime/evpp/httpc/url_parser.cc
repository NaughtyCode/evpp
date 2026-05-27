#include "runtime/evpp/httpc/url_parser.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <string>

namespace evpp {
namespace httpc {

static const std::string default_http_port = "80";
static bool equal_key(char v) {
	return v == ':' || v == '/' || v == '?' || v == '#';
}

static bool is_question_or_sharp(char v) {
	return v == '?' || v == '#';
}

URLParser::URLParser(const std::string& url) : port(80) {
	parse(url);
}

int URLParser::parse(const std::string& url_s) {
	std::string::const_iterator it;
	std::string::const_iterator last_it = url_s.begin();

	static const std::string prot_end("://");
	it = std::search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());
	if (it != url_s.end()) {
		schema.reserve(std::distance(url_s.begin(), it));
		std::transform(url_s.begin(), it, std::back_inserter(schema), [](unsigned char c) {
			return std::tolower(c);
		});	 // protocol is icase
		std::advance(it, prot_end.length());
		last_it = it;
	}

	it = std::find_if(last_it, url_s.end(), equal_key);

	host.reserve(std::distance(last_it, it));
	std::transform(last_it, it, std::back_inserter(host), [](unsigned char c) {
		return std::tolower(c);
	});	 // host is icase

	if (it == url_s.end()) {
		return 0;
	}

	if (*it == ':') {
		it++;

		if (it != url_s.end()) {
			last_it = it;
			it = std::find_if(last_it, url_s.end(), equal_key);
			std::string port_str(last_it, it);
			char* end = nullptr;
			long p = std::strtol(port_str.c_str(), &end, 10);
			if (end != port_str.c_str() && *end == '\0' && p > 0 && p <= 65535) {
				port = static_cast<int>(p);
			}
		}
	}

	if (it != url_s.end() && *it == '/') {
		last_it = it;
		it = std::find_if(last_it, url_s.end(), is_question_or_sharp);
		path.assign(last_it, it);
	}

	if (it != url_s.end() && *it == '?') {
		it++;

		if (it != url_s.end()) {
			last_it = it;
			auto frag_it =
				std::find_if(last_it, url_s.end(), [](unsigned char c) { return c == '#'; });
			query.assign(last_it, frag_it);
			it = frag_it;
		}
	}

	return 0;
}
}
}
