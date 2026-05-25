#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// sockaddr is used directly — include the right header per platform
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// Forward-declare iovec wrapping
struct MongoIovec;

// Wraps mongoc-socket.h — low-level socket abstraction with timeout support.
class ENGINE_API MongoSocket {
public:
    static MongoSocket* New(int domain, int type, int protocol);
    void Destroy();

    MongoSocket(const MongoSocket&) = delete;
    MongoSocket& operator=(const MongoSocket&) = delete;
    MongoSocket(MongoSocket&&) = delete;
    MongoSocket& operator=(MongoSocket&&) = delete;

    MongoSocket* Accept(int64_t expire_at);
    int Bind(const struct sockaddr* addr, int addrlen);
    int Close();
    int Connect(const struct sockaddr* addr, int addrlen, int64_t expire_at);
    char* GetNameInfo(); // Caller must bson_free() the returned string
    int GetError() const;
    int GetSockName(struct sockaddr* addr, int* addrlen) const;
    int Listen(unsigned int backlog);
    ssize_t Receive(void* buf, size_t buflen, int flags, int64_t expire_at);
    ssize_t SendData(const void* buf, size_t buflen, int64_t expire_at);
    ssize_t SendvData(MongoIovec* iov, size_t iovcnt, int64_t expire_at);
    int SetSockOpt(int level, int optname, const void* optval, int optlen);
    bool CheckClosed();

    static void InetNtop(struct addrinfo* rp, char* buf, size_t buflen);
    static ssize_t PollFds(MongoSocketPollFd* sds, size_t nsds, int32_t timeout);

    void* Raw(); // returns mongoc_socket_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoSocket();
    ~MongoSocket();
};

// Mirrors mongoc_socket_poll_t.
struct ENGINE_API MongoSocketPollFd {
    MongoSocket* socket = nullptr;
    int events = 0;
    int revents = 0;
};

} // namespace mongo
} // namespace engine
