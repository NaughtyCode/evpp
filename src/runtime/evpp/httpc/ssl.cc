#include "runtime/evpp/httpc/ssl.h"

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
#include "runtime/core/log/log.h"

#include <openssl/err.h>
#include <openssl/rand.h>

namespace evpp {
namespace httpc {
static SSL_CTX* g_ssl_ctx = nullptr;

bool InitSSL() {
	SSL_library_init();
	ERR_load_crypto_strings();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	int r = RAND_poll();
	if (r == 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "RAND_poll failed");
		return false;
	}
	g_ssl_ctx = SSL_CTX_new(SSLv23_method());
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
	ERR_free_strings();
	EVP_cleanup();
	ERR_remove_thread_state(nullptr);
	CRYPTO_cleanup_all_ex_data();
}

SSL_CTX* GetSSLCtx() {
	return g_ssl_ctx;
}
}  // httpc
}  // evpp

#endif
