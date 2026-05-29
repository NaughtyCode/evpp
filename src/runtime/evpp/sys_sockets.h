#pragma once


#include "runtime/evpp/platform_config.h"

#ifdef H_OS_WINDOWS


#include <string>  // avoid compiling failed because of 'errno' redefined as 'WSAGetLastError()'

#define EVPP_ERRNO WSAGetLastError()

#include <WinSock2.h>
#include <io.h>
#include <mstcpip.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#define iovec _WSABUF
#define iov_base buf
#define iov_len len

#else
#include <fcntl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/uio.h>
#ifndef SOCKET
#define SOCKET int /**< SOCKET definition */
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1 /**< invalid socket definition */
#endif

#define EVPP_ERRNO errno

#endif

#ifdef H_OS_WINDOWS

#define gai_strerror gai_strerrorA

#endif	// end of H_OS_WINDOWS

#if (defined(H_OS_WINDOWS) || defined(H_OS_MACOSX))

#ifndef HAVE_MSG_NOSIGNAL
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#ifndef HAVE_MSG_DONTWAIT
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#endif

#endif


// Copied from libevent2.0/util-internal.h
#ifdef H_OS_WINDOWS

#define EVUTIL_ERR_RW_RETRIABLE(e) ((e) == WSAEWOULDBLOCK || (e) == WSAETIMEDOUT || (e) == WSAEINTR)

#define EVUTIL_ERR_CONNECT_RETRIABLE(e) \
	((e) == WSAEWOULDBLOCK || (e) == WSAEINTR || (e) == WSAEINPROGRESS || (e) == WSAEINVAL)

#define EVUTIL_ERR_ACCEPT_RETRIABLE(e) EVUTIL_ERR_RW_RETRIABLE(e)

#define EVUTIL_ERR_CONNECT_REFUSED(e) ((e) == WSAECONNREFUSED)

#else

/* True iff e is an error that means a read/write operation can be retried. */
#define EVUTIL_ERR_RW_RETRIABLE(e) ((e) == EINTR || (e) == EAGAIN)
/* True iff e is an error that means a connect can be retried. */
#define EVUTIL_ERR_CONNECT_RETRIABLE(e) ((e) == EINTR || (e) == EINPROGRESS)
/* True iff e is an error that means an accept can be retried. */
#define EVUTIL_ERR_ACCEPT_RETRIABLE(e) ((e) == EINTR || (e) == EAGAIN || (e) == ECONNABORTED)

/* True iff e is an error that means the connection was refused */
#define EVUTIL_ERR_CONNECT_REFUSED(e) ((e) == ECONNREFUSED)

#endif


#ifdef H_OS_WINDOWS
#define evpp_socket_t intptr_t
#else
#define evpp_socket_t int
#endif

#define signal_number_t evpp_socket_t