#pragma once

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)

#include <memory>
#include <string>

#include <openssl/ssl.h>

struct event_base;

namespace evpp {

class SSLContext {
public:
    enum Role { kServer, kClient };

    SSLContext();
    ~SSLContext();

    SSLContext(const SSLContext&) = delete;
    SSLContext& operator=(const SSLContext&) = delete;

    bool Init(Role role,
              const std::string& cert_file,
              const std::string& key_file,
              const std::string& ca_file  = "",
              bool verify_peer             = false);

    SSL* CreateSSL();
    SSL_CTX* raw_ctx() const { return ctx_; }

    bool valid() const { return ctx_ != nullptr; }

private:
    SSL_CTX* ctx_ = nullptr;
};

}  // namespace evpp

#endif  // EVPP_HTTP_CLIENT_SUPPORTS_SSL
