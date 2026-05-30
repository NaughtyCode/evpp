// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

// Modified : zieckey (zieckey at gmail dot com)

#include "runtime/evpp/buffer.h"

#include <algorithm>
#include <limits>

#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/sockets.h"

namespace evpp {
const size_t Buffer::kCheapPrependSize;
const size_t Buffer::kInitialSize;
const char* const Buffer::kCRLF = "\r\n";

ssize_t Buffer::ReadFromFD(evpp_socket_t fd, int* savedErrno) {
	// saved an ioctl()/FIONREAD call to tell how much to read
	char extrabuf[65536];
	struct iovec vec[2];
	const size_t writable = WritableBytes();
	const size_t readable = length();
	if (max_capacity_ > 0 && readable >= max_capacity_) {
		if (savedErrno) *savedErrno = 0;
		return 0;
	}

	const size_t iov_len_max = static_cast<size_t>((std::numeric_limits<unsigned long>::max)());
	const size_t max_read =
		max_capacity_ > 0 ? (std::min)(max_capacity_ - readable, writable + sizeof extrabuf)
						  : writable + sizeof extrabuf;
	if (max_read == 0) {
		if (savedErrno) *savedErrno = 0;
		return 0;
	}

	const size_t inline_len = (std::min)((std::min)(writable, max_read), iov_len_max);
	int iovcnt = 0;
	if (inline_len > 0) {
		vec[iovcnt].iov_base = begin() + write_index_;
		vec[iovcnt].iov_len = static_cast<unsigned long>(inline_len);
		++iovcnt;
	}

	const size_t extra_len = (std::min)(sizeof extrabuf, max_read - inline_len);
	if (extra_len > 0) {
		vec[iovcnt].iov_base = extrabuf;
		vec[iovcnt].iov_len = static_cast<unsigned long>(extra_len);
		++iovcnt;
	}

	// when there is enough space in this buffer, don't read into extrabuf.
	// when extrabuf is used, we read 64k bytes at most.
	const ssize_t n = ::readv(fd, vec, iovcnt);

	if (n < 0) {
		if (savedErrno) {
			*savedErrno = EVPP_ERRNO;
		}
	} else if (static_cast<size_t>(n) <= inline_len) {
		write_index_ += n;
	} else {
		write_index_ += inline_len;
		Append(extrabuf, static_cast<size_t>(n) - inline_len);
	}

	return n;
}
}
