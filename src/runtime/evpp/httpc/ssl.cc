#include "runtime/evpp/httpc/ssl.h"

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
#include "runtime/core/log/log.h"

#include <openssl/err.h>
#include <openssl/rand.h>

namespace evpp {
namespace httpc {
static SSL_CTX* g_ssl_ctx = nullptr;

bool InitSSL() {
	// BoringSSL auto-initializes; no explicit library/algorithm init needed.
	// RAND_poll may be a no-op on some platforms but is harmless.
	int r = RAND_poll();
	if (r == 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "RAND_poll failed");
		return false;
	}
	g_ssl_ctx = SSL_CTX_new(TLS_method());
	if (!g_ssl_ctx) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "SSL_CTX_new failed");
		return false;
	}
	X509_STORE* store = SSL_CTX_get_cert_store(g_ssl_ctx);
	if (X509_STORE_set_default_paths(store) != 1) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "X509_STORE_set_default_paths failed");
		SSL_CTX_free(g_ssl_ctx);
		g_ssl_ctx = nullptr;
		return false;
	}
	return true;
}

void CleanSSL() {
	if (g_ssl_ctx != nullptr) {
		SSL_CTX_free(g_ssl_ctx);
		g_ssl_ctx = nullptr;
	}
	// BoringSSL auto-cleans up; no explicit ERR/EVP/CRYPTO cleanup needed.
}

SSL_CTX* GetSSLCtx() {
	return g_ssl_ctx;
}
}  // namespace httpc
}  // namespace evpp

#endif
