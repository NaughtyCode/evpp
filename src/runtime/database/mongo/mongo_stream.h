#pragma once

#include <cstdint>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Wraps mongoc_stream_t and all specialized stream types (buffered, file,
// GridFS, socket, TLS, and platform-specific TLS backends).
//
// All constructors are static factory methods that return a new MongoStream*
// (call Destroy() to release). Operations that only apply to specific stream
// types (GetFileFd, GetSocket, TlsHandshake, etc.) are safe to call on any
// stream — the underlying C driver validates the type.
class ENGINE_API MongoStream {
public:
    // ── Factory methods ──────────────────────────────────────────────

    static MongoStream* NewBuffered(MongoStream* base_stream, size_t buffer_size);
    static MongoStream* NewFile(int fd);
    static MongoStream* NewFileForPath(const char* path, int flags, int mode);

    // GridFS stream — takes ownership of the file handle (do not destroy
    // the MongoGridFsFile after passing it here).
    static MongoStream* NewGridFs(void* gridfs_file); // mongoc_gridfs_file_t*

    static MongoStream* NewSocket(void* socket); // mongoc_socket_t*

    // TLS stream constructors (general + platform-specific).
    static MongoStream* NewTls(MongoStream* base_stream, const char* host,
                                void* ssl_opts, int client);
    static MongoStream* NewTlsOpenssl(MongoStream* base_stream, const char* host,
                                       void* ssl_opts, int client);
    static MongoStream* NewTlsSecureChannel(MongoStream* base_stream, const char* host,
                                              void* ssl_opts, int client);
    static MongoStream* NewTlsSecureTransport(MongoStream* base_stream, const char* host,
                                                void* ssl_opts, int client);

    void Destroy();

    // Non-copyable, non-movable (heap-allocated with Destroy()).
    MongoStream(const MongoStream&) = delete;
    MongoStream& operator=(const MongoStream&) = delete;
    MongoStream(MongoStream&&) = delete;
    MongoStream& operator=(MongoStream&&) = delete;

    // ── Base stream operations ───────────────────────────────────────
    MongoStream* GetBaseStream();
    MongoStream* GetTlsStream();
    int Close();
    void Failed();
    int Flush();
    ssize_t Writev(void* iov, size_t iovcnt, int32_t timeout_msec);
    ssize_t Write(void* buf, size_t count, int32_t timeout_msec);
    ssize_t Readv(void* iov, size_t iovcnt, size_t min_bytes, int32_t timeout_msec);
    ssize_t Read(void* buf, size_t count, size_t min_bytes, int32_t timeout_msec);
    int SetSockopt(int level, int optname, void* optval, int optlen);
    bool CheckClosed();
    bool TimedOut();
    bool ShouldRetry();

    // ── TLS operations ───────────────────────────────────────────────
    bool TlsHandshake(const char* host, int32_t timeout_msec, int* events,
                       MongoError* error);
    bool TlsHandshakeBlock(const char* host, int32_t timeout_msec,
                            MongoError* error);

    // ── File stream operations ───────────────────────────────────────
    int GetFileFd();

    // ── Socket stream operations ─────────────────────────────────────
    void* GetSocket(); // returns mongoc_socket_t*

    // ── Static helpers ───────────────────────────────────────────────
    // Poll a set of streams. `streams` is an array of mongoc_stream_poll_t.
    static ssize_t Poll(void* streams, size_t nstreams, int32_t timeout_ms);

    void* Raw(); // returns mongoc_stream_t*

    // ── Ownership helpers (for internal use) ─────────────────────────
    void* ReleaseStream(); // releases ownership, returns mongoc_stream_t*
    void SetRawStream(void* stream); // takes ownership of mongoc_stream_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoStream();
    ~MongoStream();
};

} // namespace mongo
} // namespace engine
