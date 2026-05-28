// Modified from muduo project http://github.com/chenshuo/muduo
// @see https://github.com/chenshuo/muduo/blob/master/muduo/net/Buffer.h and https://github.com/chenshuo/muduo/blob/master/muduo/net/Buffer.cc

#pragma once

#include <algorithm>
#include <cstdint>

#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/slice.h"
#include "runtime/evpp/sockets.h"

namespace evpp {
class EVPP_EXPORT Buffer {
	public:
	static const size_t kCheapPrependSize;
	static const size_t kInitialSize;

	explicit Buffer(size_t initial_size = kInitialSize,
					size_t reserved_prepend_size = kCheapPrependSize)
		: capacity_(reserved_prepend_size + initial_size),
		  read_index_(reserved_prepend_size),
		  write_index_(reserved_prepend_size),
		  reserved_prepend_size_(reserved_prepend_size), max_capacity_(256 * 1024) {
		buffer_ = MEM_NEW_ARR(char, capacity_);
		assert(length() == 0);
		assert(WritableBytes() == initial_size);
		assert(PrependableBytes() == reserved_prepend_size);
	}

	~Buffer() {
		MEM_DELETE_ARR(buffer_);
		buffer_ = nullptr;
		capacity_ = 0;
	}

	// Non-copyable (owns a raw buffer).  Use Swap() or move.
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	Buffer(Buffer&& rhs) noexcept
		: buffer_(rhs.buffer_),
		  capacity_(rhs.capacity_),
		  read_index_(rhs.read_index_),
		  write_index_(rhs.write_index_),
		  reserved_prepend_size_(rhs.reserved_prepend_size_) {
		rhs.buffer_ = nullptr;
		rhs.capacity_ = 0;
		rhs.read_index_ = 0;
		rhs.write_index_ = 0;
		rhs.reserved_prepend_size_ = 0;
	}

	Buffer& operator=(Buffer&& rhs) noexcept {
		if (this != &rhs) {
			MEM_DELETE_ARR(buffer_);
			buffer_ = rhs.buffer_;
			capacity_ = rhs.capacity_;
			read_index_ = rhs.read_index_;
			write_index_ = rhs.write_index_;
			reserved_prepend_size_ = rhs.reserved_prepend_size_;
			rhs.buffer_ = nullptr;
			rhs.capacity_ = 0;
			rhs.read_index_ = 0;
			rhs.write_index_ = 0;
			rhs.reserved_prepend_size_ = 0;
		}
		return *this;
	}

	void Swap(Buffer& rhs) {
		std::swap(buffer_, rhs.buffer_);
		std::swap(capacity_, rhs.capacity_);
		std::swap(read_index_, rhs.read_index_);
		std::swap(write_index_, rhs.write_index_);
		std::swap(reserved_prepend_size_, rhs.reserved_prepend_size_);
	}

	// Skip advances the reading index of the buffer
	void Skip(size_t len) {
		assert(len <= length());
		if (len < length()) {
			read_index_ += len;
		} else {
			Reset();
		}
	}

	// Retrieve advances the reading index of the buffer
	// Retrieve it the same as Skip.
	void Retrieve(size_t len) {
		Skip(len);
	}

	// Truncate discards all but the first n unread bytes from the buffer
	// but continues to use the same allocated storage.
	// It does nothing if n is greater than the length of the buffer.
	void Truncate(size_t n) {
		if (n == 0) {
			read_index_ = reserved_prepend_size_;
			write_index_ = reserved_prepend_size_;
		} else if (write_index_ > read_index_ + n) {
			write_index_ = read_index_ + n;
		}
	}

	// Reset resets the buffer to be empty,
	// but it retains the underlying storage for use by future writes.
	// Reset is the same as Truncate(0).
	void Reset() {
		Truncate(0);
	}

	// Increase the capacity of the container to a value that's greater
	// or equal to len. If len is greater than the current capacity(),
	// new storage is allocated, otherwise the method does nothing.
	void Reserve(size_t len) {
		if (capacity_ >= len + reserved_prepend_size_) {
			return;
		}

		grow(len + reserved_prepend_size_);
	}

	// Make sure there is enough memory space to append more data with length len
	void EnsureWritableBytes(size_t len) {
		if (WritableBytes() < len) {
			grow(len);
		}

		assert(WritableBytes() >= len);
	}

	// ToText appends char '\0' to buffer to convert the underlying data to a c-style string text.
	// It will not change the length of buffer.
	void ToText() {
		AppendInt8('\0');
		UnwriteBytes(1);
	}

	// Convert 64-bit integer between host and network byte order.
	// On little-endian systems (x86, ARM64 in LE mode) this byte-swaps;
	// on big-endian systems (s390x, some ARM configs) this is a no-op.
	static uint64_t HostToNetwork64(uint64_t host64) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		return host64;
#elif defined(_MSC_VER)
		return _byteswap_uint64(host64);
#else
		return __builtin_bswap64(host64);
#endif
	}

	static uint32_t HostToNetwork32(uint32_t host32) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		return host32;
#else
		return htonl(host32);
#endif
	}

	static uint16_t HostToNetwork16(uint16_t host16) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		return host16;
#else
		return htons(host16);
#endif
	}

	static uint64_t NetworkToHost64(uint64_t net64) {
		return HostToNetwork64(net64);
	}

	static uint32_t NetworkToHost32(uint32_t net32) {
		return HostToNetwork32(net32);
	}

	static uint16_t NetworkToHost16(uint16_t net16) {
		return HostToNetwork16(net16);
	}

	// Write
	public:
	void Write(const void* /*restrict*/ d, size_t len) {
		EnsureWritableBytes(len);
		memcpy(WriteBegin(), d, len);
		assert(write_index_ + len <= capacity_);
		write_index_ += len;
	}

	void Append(const Slice& str) {
		Write(str.data(), str.size());
	}

	void Append(const char* /*restrict*/ d, size_t len) {
		Write(d, len);
	}

	void Append(const void* /*restrict*/ d, size_t len) {
		Write(d, len);
	}

	// Append int64_t/int32_t/int16_t with network endian
	void AppendInt64(int64_t x) {
		int64_t be = static_cast<int64_t>(HostToNetwork64(static_cast<uint64_t>(x)));
		Write(&be, sizeof be);
	}

	void AppendInt32(int32_t x) {
		int32_t be32 = htonl(x);
		Write(&be32, sizeof be32);
	}

	void AppendInt16(int16_t x) {
		int16_t be16 = htons(x);
		Write(&be16, sizeof be16);
	}

	void AppendInt8(int8_t x) {
		Write(&x, sizeof x);
	}

	// Prepend int64_t/int32_t/int16_t with network endian
	void PrependInt64(int64_t x) {
		int64_t be = static_cast<int64_t>(HostToNetwork64(static_cast<uint64_t>(x)));
		Prepend(&be, sizeof be);
	}

	void PrependInt32(int32_t x) {
		int32_t be32 = htonl(x);
		Prepend(&be32, sizeof be32);
	}

	void PrependInt16(int16_t x) {
		int16_t be16 = htons(x);
		Prepend(&be16, sizeof be16);
	}

	void PrependInt8(int8_t x) {
		Prepend(&x, sizeof x);
	}

	// Insert content, specified by the parameter, into the front of reading index
	void Prepend(const void* /*restrict*/ d, size_t len) {
		assert(len <= PrependableBytes());
		read_index_ -= len;
		const char* p = static_cast<const char*>(d);
		memcpy(begin() + read_index_, p, len);
	}

	void UnwriteBytes(size_t n) {
		assert(n <= length());
		write_index_ -= n;
	}

	void WriteBytes(size_t n) {
		assert(n <= WritableBytes());
		write_index_ += n;
	}

	//Read
	public:
	// Peek int64_t/int32_t/int16_t/int8_t with network endian
	int64_t ReadInt64() {
		int64_t result = PeekInt64();
		Skip(sizeof result);
		return result;
	}

	int32_t ReadInt32() {
		int32_t result = PeekInt32();
		Skip(sizeof result);
		return result;
	}

	int16_t ReadInt16() {
		int16_t result = PeekInt16();
		Skip(sizeof result);
		return result;
	}

	int8_t ReadInt8() {
		int8_t result = PeekInt8();
		Skip(sizeof result);
		return result;
	}

	Slice ToSlice() const {
		return Slice(data(), length());
	}

	std::string ToString() const {
		return std::string(data(), length());
	}

	void Shrink(size_t reserve) {
		Buffer other(length() + reserve);
		other.Append(ToSlice());
		Swap(other);
	}

	// ReadFromFD reads data from a fd directly into buffer,
	// and return result of readv, errno is saved into saved_errno
	ssize_t ReadFromFD(evpp_socket_t fd, int* saved_errno);

		/* Maximum total capacity in bytes. When length() reaches this
		 * limit, ReadFromFD returns 0 (no more data read). Default 256KB. */
		void SetMaxCapacity(size_t max) { max_capacity_ = max; }
		size_t GetMaxCapacity() const { return max_capacity_; }
		bool AtMaxCapacity() const { return length() >= max_capacity_; }

	// Next returns a slice containing the next n bytes from the buffer,
	// advancing the buffer as if the bytes had been returned by Read.
	// If there are fewer than n bytes in the buffer, Next returns the entire buffer.
	// The slice is only valid until the next call to a read or write method.
	Slice Next(size_t len) {
		if (len < length()) {
			Slice result(data(), len);
			read_index_ += len;
			return result;
		}

		return NextAll();
	}

	// NextAll returns a slice containing all the unread portion of the buffer,
	// advancing the buffer as if the bytes had been returned by Read.
	Slice NextAll() {
		Slice result(data(), length());
		Reset();
		return result;
	}

	std::string NextString(size_t len) {
		Slice s = Next(len);
		return std::string(s.data(), s.size());
	}

	std::string NextAllString() {
		Slice s = NextAll();
		return std::string(s.data(), s.size());
	}

	// ReadByte reads and returns the next byte from the buffer.
	// If no byte is available, it returns '\0'.
	char ReadByte() {
		if (length() == 0) {
			return '\0';
		}

		return buffer_[read_index_++];
	}

	// UnreadBytes unreads the last n bytes returned
	// by the most recent read operation.
	void UnreadBytes(size_t n) {
		assert(n <= read_index_);
		read_index_ -= n;
	}

	// Peek
	public:
	// Peek int64_t/int32_t/int16_t/int8_t with network endian

	int64_t PeekInt64() const {
		if (length() < sizeof(int64_t)) return 0;
		int64_t be64 = 0;
		::memcpy(&be64, data(), sizeof be64);
		return static_cast<int64_t>(NetworkToHost64(static_cast<uint64_t>(be64)));
	}

	int32_t PeekInt32() const {
		if (length() < sizeof(int32_t)) return 0;
		int32_t be32 = 0;
		::memcpy(&be32, data(), sizeof be32);
		return ntohl(be32);
	}

	int16_t PeekInt16() const {
		if (length() < sizeof(int16_t)) return 0;
		int16_t be16 = 0;
		::memcpy(&be16, data(), sizeof be16);
		return ntohs(be16);
	}

	int8_t PeekInt8() const {
		if (length() < sizeof(int8_t)) return 0;
		int8_t x = *data();
		return x;
	}

	public:
	// data returns a pointer of length Buffer.length() holding the unread portion of the buffer.
	// The data is valid for use only until the next buffer modification (that is,
	// only until the next call to a method like Read, Write, Reset, or Truncate).
	// The data aliases the buffer content at least until the next buffer modification,
	// so immediate changes to the slice will affect the result of future reads.
	const char* data() const {
		return buffer_ + read_index_;
	}

	char* WriteBegin() {
		return begin() + write_index_;
	}

	const char* WriteBegin() const {
		return begin() + write_index_;
	}

	// length returns the number of bytes of the unread portion of the buffer
	size_t length() const {
		assert(write_index_ >= read_index_);
		return write_index_ - read_index_;
	}

	// size returns the number of bytes of the unread portion of the buffer.
	// It is the same as length().
	size_t size() const {
		return length();
	}

	// capacity returns the capacity of the buffer's underlying byte slice, that is, the
	// total space allocated for the buffer's data.
	size_t capacity() const {
		return capacity_;
	}

	size_t WritableBytes() const {
		assert(capacity_ >= write_index_);
		return capacity_ - write_index_;
	}

	size_t PrependableBytes() const {
		return read_index_;
	}

	// Helpers
	public:
	const char* FindCRLF() const {
		const char* crlf = std::search(data(), WriteBegin(), kCRLF, kCRLF + 2);
		return crlf == WriteBegin() ? nullptr : crlf;
	}

	const char* FindCRLF(const char* start) const {
		assert(data() <= start);
		assert(start <= WriteBegin());
		const char* crlf = std::search(start, WriteBegin(), kCRLF, kCRLF + 2);
		return crlf == WriteBegin() ? nullptr : crlf;
	}

	const char* FindEOL() const {
		const void* eol = memchr(data(), '\n', length());
		return static_cast<const char*>(eol);
	}

	const char* FindEOL(const char* start) const {
		assert(data() <= start);
		assert(start <= WriteBegin());
		const void* eol = memchr(start, '\n', WriteBegin() - start);
		return static_cast<const char*>(eol);
	}

	private:
	char* begin() {
		return buffer_;
	}

	const char* begin() const {
		return buffer_;
	}

	void grow(size_t len) {
		if (WritableBytes() + PrependableBytes() < len + reserved_prepend_size_) {
			//grow the capacity
			// Guard against overflow: when capacity_ passes SIZE_MAX/2,
			// (capacity_ << 1) wraps.  Fall back to linear growth instead.
			size_t n;
			if (capacity_ > SIZE_MAX / 2) {
				n = capacity_ + len;
				if (n < capacity_) n = SIZE_MAX;  // overflow clamp
			} else {
				n = (capacity_ << 1) + len;
			}
			size_t m = length();
			char* d = MEM_NEW_ARR(char, n);
			memcpy(d + reserved_prepend_size_, begin() + read_index_, m);
			write_index_ = m + reserved_prepend_size_;
			read_index_ = reserved_prepend_size_;
			capacity_ = n;
			MEM_DELETE_ARR(buffer_);
			buffer_ = d;
		} else {
			// move readable data to the front, make space inside buffer
			assert(reserved_prepend_size_ < read_index_);
			size_t readable = length();
			memmove(begin() + reserved_prepend_size_, begin() + read_index_, length());
			read_index_ = reserved_prepend_size_;
			write_index_ = read_index_ + readable;
			assert(readable == length());
			assert(WritableBytes() >= len);
		}
	}

	private:
	char* buffer_;
	size_t capacity_;
	size_t read_index_;
	size_t write_index_;
	size_t reserved_prepend_size_;
		size_t max_capacity_;
	static const char kCRLF[];
};

}
