#include "runtime/database/mongo/mongo_socket.h"

#include <mongoc/mongoc.h>

#include "runtime/database/mongo/mongo_iovec.h"

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoSocket
// ═══════════════════════════════════════════════════════════════════════

struct MongoSocket::Impl {
    mongoc_socket_t* sock = nullptr;
};

MongoSocket* MongoSocket::New(int domain, int type, int protocol) {
    auto* s = new MongoSocket();
    s->impl_->sock = mongoc_socket_new(domain, type, protocol);
    if (!s->impl_->sock) { delete s; return nullptr; }
    return s;
}

MongoSocket::MongoSocket() : impl_(std::make_unique<Impl>()) {}
MongoSocket::~MongoSocket() { Destroy(); }

void MongoSocket::Destroy() {
    if (impl_ && impl_->sock) {
        mongoc_socket_destroy(impl_->sock);
        impl_->sock = nullptr;
    }
}

MongoSocket* MongoSocket::Accept(int64_t expire_at) {
    if (!impl_ || !impl_->sock) return nullptr;
    auto* raw = mongoc_socket_accept(impl_->sock, expire_at);
    if (!raw) return nullptr;
    auto* result = new MongoSocket();
    result->impl_->sock = raw;
    return result;
}

int MongoSocket::Bind(const struct sockaddr* addr, int addrlen) {
    return impl_ && impl_->sock ? mongoc_socket_bind(impl_->sock, addr, addrlen) : -1;
}

int MongoSocket::Close() {
    return impl_ && impl_->sock ? mongoc_socket_close(impl_->sock) : -1;
}

int MongoSocket::Connect(const struct sockaddr* addr, int addrlen, int64_t expire_at) {
    return impl_ && impl_->sock ? mongoc_socket_connect(impl_->sock, addr, addrlen, expire_at) : -1;
}

char* MongoSocket::GetNameInfo() {
    return impl_ && impl_->sock ? mongoc_socket_getnameinfo(impl_->sock) : nullptr;
}

int MongoSocket::GetError() const {
    return impl_ && impl_->sock ? mongoc_socket_errno(impl_->sock) : 0;
}

int MongoSocket::GetSockName(struct sockaddr* addr, int* addrlen) const {
    return impl_ && impl_->sock ? mongoc_socket_getsockname(impl_->sock, addr,
        reinterpret_cast<mongoc_socklen_t*>(addrlen)) : -1;
}

int MongoSocket::Listen(unsigned int backlog) {
    return impl_ && impl_->sock ? mongoc_socket_listen(impl_->sock, backlog) : -1;
}

ssize_t MongoSocket::Receive(void* buf, size_t buflen, int flags, int64_t expire_at) {
    return impl_ && impl_->sock
        ? mongoc_socket_recv(impl_->sock, buf, buflen, flags, expire_at) : -1;
}

ssize_t MongoSocket::SendData(const void* buf, size_t buflen, int64_t expire_at) {
    return impl_ && impl_->sock
        ? mongoc_socket_send(impl_->sock, buf, buflen, expire_at) : -1;
}

ssize_t MongoSocket::SendvData(MongoIovec* iov, size_t iovcnt, int64_t expire_at) {
    return impl_ && impl_->sock
        ? mongoc_socket_sendv(impl_->sock,
            reinterpret_cast<mongoc_iovec_t*>(iov), iovcnt, expire_at)
        : -1;
}

int MongoSocket::SetSockOpt(int level, int optname, const void* optval, int optlen) {
    return impl_ && impl_->sock
        ? mongoc_socket_setsockopt(impl_->sock, level, optname, optval, optlen) : -1;
}

bool MongoSocket::CheckClosed() {
    return impl_ && impl_->sock && mongoc_socket_check_closed(impl_->sock);
}

void MongoSocket::InetNtop(struct addrinfo* rp, char* buf, size_t buflen) {
    mongoc_socket_inet_ntop(rp, buf, buflen);
}

ssize_t MongoSocket::PollFds(MongoSocketPollFd* sds, size_t nsds, int32_t timeout) {
    return mongoc_socket_poll(reinterpret_cast<mongoc_socket_poll_t*>(sds), nsds, timeout);
}

void* MongoSocket::Raw() { return impl_ ? impl_->sock : nullptr; }

} // namespace mongo
} // namespace engine
