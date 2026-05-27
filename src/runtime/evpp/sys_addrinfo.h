// Copy from Chromium project.

// This is a convenience header to pull in the platform-specific headers
// that define at least:
//
//     struct addrinfo
//     struct sockaddr*
//     getaddrinfo()
//     freeaddrinfo()
//     AI_*
//     AF_*
//
// Prefer including this file instead of directly writing the #if / #else,
// since it avoids duplicating the platform-specific selections.

#pragma once

#include "runtime/evpp/platform_config.h"

#ifdef H_OS_WINDOWS
#include <ws2def.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  // for TCP_NODELAY
#include <sys/socket.h>
#endif
