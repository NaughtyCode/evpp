#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_stream.h"
#include "runtime/core/mem/mem.h"

#include <mongoc/mongoc.h>
#ifdef MONGOC_ENABLE_SSL_SECURE_CHANNEL
#define MONGOC_INSIDE
#include <mongoc/mongoc-stream-tls-secure-channel.h>
#undef MONGOC_INSIDE
#endif

#include "runtime/database/mongo/mongo_error.h"
#include "runtime/core/mem/mem.h"
#include "runtime/database/mongo/mongo_gridfs.h"
#include "runtime/core/mem/mem.h"

namespace engine {
namespace mongo {

struct MongoStream::Impl {
	mongoc_stream_t* stream = nullptr;
	bool owned = true;
};

MongoStream::MongoStream() : impl_(std::make_unique<Impl>()) {
}

MongoStream::~MongoStream() {
	if (impl_ && impl_->stream && impl_->owned) {
		mongoc_stream_destroy(impl_->stream);
		impl_->stream = nullptr;
	}
}

void MongoStream::Destroy() {
	MEM_DELETE(this);
}

void* MongoStream::ReleaseStream() {
	if (!impl_) return nullptr;
	auto* s = impl_->stream;
	impl_->stream = nullptr;
	return s;
}

void MongoStream::SetRawStream(void* stream) {
	if (!impl_) return;
	if (impl_->stream && impl_->owned) mongoc_stream_destroy(impl_->stream);
	impl_->stream = static_cast<mongoc_stream_t*>(stream);
	impl_->owned = true;
}

// ── Factory methods ──────────────────────────────────────────────────────

MongoStream* MongoStream::NewBuffered(MongoStream* base_stream, size_t buffer_size) {
	if (!base_stream || !base_stream->impl_->stream) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_buffered_new(base_stream->impl_->stream, buffer_size);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	base_stream->ReleaseStream();  // ownership transferred to buffered stream
	return result;
}

MongoStream* MongoStream::NewFile(int fd) {
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_file_new(fd);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	return result;
}

MongoStream* MongoStream::NewFileForPath(const char* path, int flags, int mode) {
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_file_new_for_path(path, flags, mode);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	return result;
}

MongoStream* MongoStream::NewGridFs(void* gridfs_file) {
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream =
		mongoc_stream_gridfs_new(static_cast<mongoc_gridfs_file_t*>(gridfs_file));
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	return result;
}

MongoStream* MongoStream::NewSocket(void* socket) {
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_socket_new(static_cast<mongoc_socket_t*>(socket));
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	return result;
}

MongoStream* MongoStream::NewTls(MongoStream* base_stream,
								 const char* host,
								 void* ssl_opts,
								 int client) {
	if (!base_stream || !base_stream->impl_->stream) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_tls_new_with_hostname(
		base_stream->impl_->stream, host, static_cast<mongoc_ssl_opt_t*>(ssl_opts), client);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	base_stream->ReleaseStream();  // ownership transferred to TLS stream
	return result;
}

MongoStream* MongoStream::NewTlsOpenssl(MongoStream* base_stream,
										const char* host,
										void* ssl_opts,
										int client) {
#ifdef MONGOC_ENABLE_SSL_OPENSSL
	if (!base_stream || !base_stream->impl_->stream) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_tls_openssl_new(
		base_stream->impl_->stream, host, static_cast<mongoc_ssl_opt_t*>(ssl_opts), client);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	base_stream->ReleaseStream();  // ownership transferred to TLS stream
	return result;
#else
	(void) base_stream;
	(void) host;
	(void) ssl_opts;
	(void) client;
	return nullptr;
#endif
}

MongoStream* MongoStream::NewTlsSecureChannel(MongoStream* base_stream,
											  const char* host,
											  void* ssl_opts,
											  int client) {
#ifdef MONGOC_ENABLE_SSL_SECURE_CHANNEL
	if (!base_stream || !base_stream->impl_->stream) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_tls_secure_channel_new(
		base_stream->impl_->stream, host, static_cast<mongoc_ssl_opt_t*>(ssl_opts), client);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	base_stream->ReleaseStream();  // ownership transferred to TLS stream
	return result;
#else
	(void) base_stream;
	(void) host;
	(void) ssl_opts;
	(void) client;
	return nullptr;
#endif
}

MongoStream* MongoStream::NewTlsSecureTransport(MongoStream* base_stream,
												const char* host,
												void* ssl_opts,
												int client) {
#ifdef MONGOC_ENABLE_SSL_SECURE_TRANSPORT
	if (!base_stream || !base_stream->impl_->stream) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = mongoc_stream_tls_secure_transport_new(
		base_stream->impl_->stream, host, static_cast<mongoc_ssl_opt_t*>(ssl_opts), client);
	if (!result->impl_->stream) {
		MEM_DELETE(result);
		return nullptr;
	}
	base_stream->ReleaseStream();  // ownership transferred to TLS stream
	return result;
#else
	(void) base_stream;
	(void) host;
	(void) ssl_opts;
	(void) client;
	return nullptr;
#endif
}

// ── Base stream operations ────────────────────────────────────────────────

MongoStream* MongoStream::GetBaseStream() {
	if (!impl_ || !impl_->stream) return nullptr;
	mongoc_stream_t* base = mongoc_stream_get_base_stream(impl_->stream);
	if (!base) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = base;
	result->impl_->owned = false;  // borrowed reference
	return result;
}

MongoStream* MongoStream::GetTlsStream() {
	if (!impl_ || !impl_->stream) return nullptr;
	mongoc_stream_t* tls = mongoc_stream_get_tls_stream(impl_->stream);
	if (!tls) return nullptr;
	auto* result = MEM_NEW(MongoStream);
	result->impl_->stream = tls;
	result->impl_->owned = false;  // borrowed reference
	return result;
}

int MongoStream::Close() {
	return impl_ && impl_->stream ? mongoc_stream_close(impl_->stream) : -1;
}

void MongoStream::Failed() {
	if (impl_ && impl_->stream) mongoc_stream_failed(impl_->stream);
}

int MongoStream::Flush() {
	return impl_ && impl_->stream ? mongoc_stream_flush(impl_->stream) : -1;
}

ssize_t MongoStream::Writev(void* iov, size_t iovcnt, int32_t timeout_msec) {
	return impl_ && impl_->stream
			   ? mongoc_stream_writev(
					 impl_->stream, static_cast<mongoc_iovec_t*>(iov), iovcnt, timeout_msec)
			   : -1;
}

ssize_t MongoStream::Write(void* buf, size_t count, int32_t timeout_msec) {
	return impl_ && impl_->stream ? mongoc_stream_write(impl_->stream, buf, count, timeout_msec)
								  : -1;
}

ssize_t MongoStream::Readv(void* iov, size_t iovcnt, size_t min_bytes, int32_t timeout_msec) {
	return impl_ && impl_->stream ? mongoc_stream_readv(impl_->stream,
														static_cast<mongoc_iovec_t*>(iov),
														iovcnt,
														min_bytes,
														timeout_msec)
								  : -1;
}

ssize_t MongoStream::Read(void* buf, size_t count, size_t min_bytes, int32_t timeout_msec) {
	return impl_ && impl_->stream
			   ? mongoc_stream_read(impl_->stream, buf, count, min_bytes, timeout_msec)
			   : -1;
}

int MongoStream::SetSockopt(int level, int optname, void* optval, int optlen) {
	return impl_ && impl_->stream
			   ? mongoc_stream_setsockopt(
					 impl_->stream, level, optname, optval, static_cast<mongoc_socklen_t>(optlen))
			   : -1;
}

bool MongoStream::CheckClosed() {
	return impl_ && impl_->stream && mongoc_stream_check_closed(impl_->stream);
}

bool MongoStream::TimedOut() {
	return impl_ && impl_->stream && mongoc_stream_timed_out(impl_->stream);
}

bool MongoStream::ShouldRetry() {
	return impl_ && impl_->stream && mongoc_stream_should_retry(impl_->stream);
}

// ── TLS operations ────────────────────────────────────────────────────────

bool MongoStream::TlsHandshake(const char* host,
							   int32_t timeout_msec,
							   int* events,
							   MongoError* error) {
	return impl_ && impl_->stream &&
		   mongoc_stream_tls_handshake(impl_->stream,
									   host,
									   timeout_msec,
									   events,
									   error ? static_cast<bson_error_t*>(error->RawError())
											 : nullptr);
}

bool MongoStream::TlsHandshakeBlock(const char* host, int32_t timeout_msec, MongoError* error) {
	return impl_ && impl_->stream &&
		   mongoc_stream_tls_handshake_block(impl_->stream,
											 host,
											 timeout_msec,
											 error ? static_cast<bson_error_t*>(error->RawError())
												   : nullptr);
}

// ── File stream operations ────────────────────────────────────────────────

int MongoStream::GetFileFd() {
	return impl_ && impl_->stream
			   ? mongoc_stream_file_get_fd(reinterpret_cast<mongoc_stream_file_t*>(impl_->stream))
			   : -1;
}

// ── Socket stream operations ──────────────────────────────────────────────

void* MongoStream::GetSocket() {
	return impl_ && impl_->stream ? mongoc_stream_socket_get_socket(
										reinterpret_cast<mongoc_stream_socket_t*>(impl_->stream))
								  : nullptr;
}

// ── Static helpers ────────────────────────────────────────────────────────

ssize_t MongoStream::Poll(void* streams, size_t nstreams, int32_t timeout_ms) {
	return mongoc_stream_poll(static_cast<mongoc_stream_poll_t*>(streams), nstreams, timeout_ms);
}

void* MongoStream::Raw() {
	return impl_ ? impl_->stream : nullptr;
}

}  // namespace mongo
}  // namespace engine

#endif
