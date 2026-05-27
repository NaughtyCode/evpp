#pragma once

// Include this header FIRST in test files that create an evpp::EventLoop
// directly. The evpp library does not call WSAStartup itself — that is
// handled by the server/client entry points. Tests must call it explicitly.
//
// Must be included before any other header on Windows, because winsock2.h
// must precede windows.h.
//
// A static initializer ensures WSAStartup is called once before main().

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

namespace {
struct TestWSAInit {
    TestWSAInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~TestWSAInit() {
        WSACleanup();
    }
};
static TestWSAInit s_test_wsa;
}  // namespace
#endif
