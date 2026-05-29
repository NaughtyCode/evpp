#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>

#include "runtime/evpp/inner_pre.h"

namespace evpp {
namespace httpc {
struct CLOUD_ENGINE_API URLParser {
	public:
	std::string schema;
	std::string host;
	int port;
	std::string path;
	std::string query;

	URLParser(const std::string& url);

	private:
	int parse(const std::string& url);
};
}  // httpc
}  // evpp