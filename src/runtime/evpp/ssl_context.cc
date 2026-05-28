#include "runtime/evpp/ssl_context.h"

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)

#include "runtime/core/log/log.h"
#include "runtime/evpp/libevent.h"

#include <openssl/err.h>

namespace evpp {

SSLContext::SSLContext() = default;

SSLContext::~SSLContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

bool SSLContext::Init(Role role,
                      const std::string& cert_file,
                      const std::string& key_file,
                      const std::string& ca_file,
                      bool verify_peer) {
    // BoringSSL provides only TLS_method(); accept/connect state is set on SSL*.
    (void)role;
    const SSL_METHOD* method = TLS_method();
    ctx_ = SSL_CTX_new(method);
    if (!ctx_) {
        ENGINE_LOG_ERROR(engine::GetLogger(), "SSL_CTX_new failed");
        return false;
    }

    SSL_CTX_set_mode(ctx_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    if (!cert_file.empty()) {
        if (SSL_CTX_use_certificate_file(ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Failed to load cert file: {}", cert_file);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
    }

    if (!key_file.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx_, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Failed to load key file: {}", key_file);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
        if (SSL_CTX_check_private_key(ctx_) != 1) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Private key does not match certificate");
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
    }

    if (!ca_file.empty()) {
        if (SSL_CTX_load_verify_locations(ctx_, ca_file.c_str(), nullptr) != 1) {
            ENGINE_LOG_ERROR(engine::GetLogger(), "Failed to load CA file: {}", ca_file);
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
    }

    if (verify_peer) {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    }

    return true;
}

SSL* SSLContext::CreateSSL() {
    if (!ctx_) return nullptr;
    return SSL_new(ctx_);
}

}  // namespace evpp

#endif  // EVPP_HTTP_CLIENT_SUPPORTS_SSL
