#include "runtime/evpp/http/service.h"

#include "runtime/evpp/libevent.h"
#include "runtime/evpp/event_watcher.h"
#include "runtime/evpp/event_loop.h"

#if defined(EVPP_HTTP_SERVER_SUPPORTS_SSL)
#include <openssl/err.h>
#endif

namespace evpp {
	namespace http {

#undef H_ARRAYSIZE
#define H_ARRAYSIZE(a) \
		((sizeof(a) / sizeof(*(a))) / \
		 static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))

		static const int kMaxHTTPCode = 1000;
		static const char* g_http_code_string[kMaxHTTPCode + 1];
		static void InitHTTPCodeString() {
			for (size_t i = 0; i < H_ARRAYSIZE(g_http_code_string); i++) {
				g_http_code_string[i] = "XX";
			}

			g_http_code_string[200] = "OK";

			g_http_code_string[302] = "Found";

			g_http_code_string[400] = "Bad Request";
			g_http_code_string[404] = "Not Found";

			//TODO Add more http code string : https://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
		}

#if defined(EVPP_HTTP_SERVER_SUPPORTS_SSL)
		Service::Service(EventLoop* l, bool enable_ssl,
					const char* certificate_chain_file, const char* private_key_file)
			: evhttp_(nullptr), evhttp_bound_socket_(nullptr), listen_loop_(l),
			enable_ssl_(enable_ssl), ssl_ctx_(nullptr),
			certificate_chain_file_(certificate_chain_file),
			private_key_file_(private_key_file) {
#else
		Service::Service(EventLoop* l)
			: evhttp_(nullptr), evhttp_bound_socket_(nullptr), listen_loop_(l) {
#endif
				evhttp_ = evhttp_new(listen_loop_->event_base());
				if (!evhttp_) {
					return;
				}

				std::once_flag flag;
				std::call_once(flag, &InitHTTPCodeString);
			}

		Service::~Service() {
			assert(!evhttp_);
			assert(!evhttp_bound_socket_);
#if defined(EVPP_HTTP_SERVER_SUPPORTS_SSL)
			if(ssl_ctx_) {
				SSL_CTX_free(ssl_ctx_);
			}
#endif
		}

#if defined(EVPP_HTTP_SERVER_SUPPORTS_SSL)
		bool Service::initSSL(bool force_enable) {
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} https service init ssl", (void*)this);
			if(force_enable) {
				if(ssl_ctx_) { SSL_CTX_free(ssl_ctx_); }
				ssl_ctx_ = nullptr;
				enable_ssl_ = true;
			}
			if(!enable_ssl_) {
				return true;
			}
			if(ssl_ctx_){ return true; };

			/* Initialize SSL protocol environment */
			// SSL_library_int();
			/* Create SSL context */
			SSL_CTX *ctx = SSL_CTX_new (SSLv23_server_method ());
			if(ctx == NULL) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "SSL_CTX_new failed");
				return false;
			}
			/* Set SSL options https://linux.die.net/man/3/ssl_ctx_set_options */
			SSL_CTX_set_options (ctx,
						SSL_OP_SINGLE_DH_USE |
						SSL_OP_SINGLE_ECDH_USE |
						SSL_OP_NO_SSLv2 /*disable SSLv2*/ |
						SSL_OP_NO_TLSv1 /*disable TLSv1*/);
			/* Whether to verify peer certificate (server-side, use SSL_VERIFY_NONE to skip verification) */
			SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
			/* Create ECDH key */
			EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);
			if (ecdh == NULL) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "EC_KEY_new_by_curve_name failed");
				ERR_print_errors_fp(stderr);
				SSL_CTX_free(ctx);
				return false;
			}
			/* Set ECDH ephemeral public key */
			if (1 != SSL_CTX_set_tmp_ecdh (ctx, ecdh)) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "SSL_CTX_set_tmp_ecdh failed");
				EC_KEY_free(ecdh);
				SSL_CTX_free(ctx);
				return false;
			}
			/* Load certificate chain file (must be PEM format, Base64 encoded) */
			/* SSL_CTX_use_certificate_file can also be used to load only the public key certificate */
			if (1 != SSL_CTX_use_certificate_chain_file (
							ctx, certificate_chain_file_.c_str())) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "Load certificate chain file({})failed.", certificate_chain_file_.c_str());
				ERR_print_errors_fp(stderr);
				EC_KEY_free(ecdh);
				SSL_CTX_free(ctx);
				return false;
			}
			/* Load private key file */
			if (1 != SSL_CTX_use_PrivateKey_file (
							ctx, private_key_file_.c_str(), SSL_FILETYPE_PEM)) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "Load private key file({})failed.", private_key_file_.c_str());
				ERR_print_errors_fp(stderr);
				EC_KEY_free(ecdh);
				SSL_CTX_free(ctx);
				return false;
			}
			/* Verify that private key matches the certificate */
			if (1 != SSL_CTX_check_private_key (ctx)) {
				ENGINE_LOG_ERROR(engine::GetLogger(), "SSL_CTX_check_private_key failed");
				ERR_print_errors_fp(stderr);
				EC_KEY_free(ecdh);
				SSL_CTX_free(ctx);
				return false;
			}
			auto bevcb = [](struct event_base *base, void *arg)
				-> struct bufferevent* {
				struct bufferevent* r;
				SSL_CTX *sslctx = (SSL_CTX *) arg;
				r = bufferevent_openssl_socket_new (base,
							-1,
							SSL_new (sslctx),
							BUFFEREVENT_SSL_ACCEPTING,
							BEV_OPT_CLOSE_ON_FREE);
				return r;
			};
			evhttp_set_bevcb (evhttp_, bevcb, ctx);
			ssl_ctx_ = ctx;
			return true;
		}
#endif

		bool Service::Listen(int listen_port) {
			assert(evhttp_);
			assert(listen_loop_->IsInLoopThread());
			port_ = listen_port;

#if defined(EVPP_HTTP_SERVER_SUPPORTS_SSL)
			if(enable_ssl_) {
				if (!initSSL()) {
					return false;
				}
			}
#endif

#if LIBEVENT_VERSION_NUMBER >= 0x02001500
			evhttp_bound_socket_ = evhttp_bind_socket_with_handle(evhttp_, "0.0.0.0", listen_port);
			if (!evhttp_bound_socket_) {
				return false;
			}
#else
			if (evhttp_bind_socket(evhttp_, "0.0.0.0", listen_port) != 0) {
				return false;
			}
#endif

			evhttp_set_gencb(evhttp_, &Service::GenericCallback, this);
			return true;
		}

		void Service::Stop() {
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} http service is stopping", (void*)this);
			assert(listen_loop_->IsInLoopThread());

			if (evhttp_) {
				evhttp_free(evhttp_);
				evhttp_ = nullptr;
				evhttp_bound_socket_ = nullptr;
			}

			callbacks_.clear();
			default_callback_ = HTTPRequestCallback();
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} http service stopped", (void*)this);
		}


		void Service::Pause() {
			assert(listen_loop_->IsInLoopThread());
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} http service pause", (void*)this);
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
			if (evhttp_bound_socket_) {
				evconnlistener_disable(evhttp_bound_socket_get_listener(evhttp_bound_socket_));
			}
#else
			ENGINE_LOG_ERROR(engine::GetLogger(), "Not support!");
			assert(false && "Not support");
#endif
		}

		void Service::Continue() {
			assert(listen_loop_->IsInLoopThread());
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} http service continue", (void*)this);
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
			if (evhttp_bound_socket_) {
				evconnlistener_enable(evhttp_bound_socket_get_listener(evhttp_bound_socket_));
			}
#else
			ENGINE_LOG_ERROR(engine::GetLogger(), "Not support!");
			assert(false && "Not support");
#endif
		}

		void Service::RegisterHandler(const std::string& uri, HTTPRequestCallback callback) {
			callbacks_[uri] = callback;
		}

		void Service::RegisterDefaultHandler(HTTPRequestCallback callback) {
			default_callback_ = callback;
		}

		void Service::GenericCallback(struct evhttp_request* req, void* arg) {
			Service* hsrv = static_cast<Service*>(arg);
			hsrv->HandleRequest(req);
		}

		void Service::HandleRequest(struct evhttp_request* req) {
			// In the main HTTP listening thread,
			// this is the main entrance of the HTTP request processing.
			assert(listen_loop_->IsInLoopThread());
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} handle request {} url={}", (void*)this, (void*)req, req->uri);

			ContextPtr ctx(new Context(req));
			ctx->Init();

			if (callbacks_.empty()) {
				DefaultHandleRequest(ctx);
				return;
			}

			auto it = callbacks_.find(ctx->uri());
			if (it != callbacks_.end()) {
				// This will forward to HTTPServer::Dispatch method to process this request.
				auto f = std::bind(&Service::SendReply, this, ctx, std::placeholders::_1);
				it->second(listen_loop_, ctx, f);
				return;
			} else {
				DefaultHandleRequest(ctx);
			}
		}

		void Service::DefaultHandleRequest(const ContextPtr& ctx) {
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} url={}", (void*)this, ctx->original_uri());
			if (default_callback_) {
				auto f = std::bind(&Service::SendReply, this, ctx, std::placeholders::_1);
				default_callback_(listen_loop_, ctx, f);
			} else {
				evhttp_send_reply(ctx->req(), HTTP_BADREQUEST, g_http_code_string[HTTP_BADREQUEST], nullptr);
			}
		}

		struct Response {
			Response(const ContextPtr& c, const std::string& m)
				: ctx(c) {
					if (m.size() > 0) {
						buffer = evbuffer_new();
						evbuffer_add(buffer, m.c_str(), m.size());
					}
				}

			~Response() {
				if (buffer) {
					evbuffer_free(buffer);
					buffer = nullptr;
				}

				// At this time, req is probably freed by evhttp framework.
				// So don't use req any more.
				// ENGINE_LOG_TRACE(engine::GetLogger(), "free request {}", req->uri);
			}

			ContextPtr ctx;
			struct evbuffer* buffer = nullptr;
		};

		void Service::SendReply(const ContextPtr& ctx, const std::string& response_data) {
			// In the worker thread
			ENGINE_LOG_TRACE(engine::GetLogger(), "this={} send reply in working thread", (void*)this);

			// Build the response package in the worker thread
			std::shared_ptr<Response> response(new Response(ctx, response_data));

			auto f = [this, response]() {
				// In the main HTTP listening thread
				assert(listen_loop_->IsInLoopThread());
				ENGINE_LOG_TRACE(engine::GetLogger(), "this={} send reply in listening thread. evhttp_={}", (void*)this, (void*)evhttp_);

				auto x = response->ctx.get();

				// At this moment, this Service maybe already stopped.
				if (!evhttp_) {
					ENGINE_LOG_WARN(engine::GetLogger(), "this={} Service has been stopped.", (void*)this);
					return;
				}

				if (!response->buffer) {
					evhttp_send_reply(x->req(), HTTP_NOTFOUND,
								g_http_code_string[HTTP_NOTFOUND], nullptr);
					return;
				}

				assert(x->response_http_code() <= kMaxHTTPCode);
				assert(x->response_http_code() >= 100);
				evhttp_send_reply(x->req(), x->response_http_code(),
							g_http_code_string[x->response_http_code()],
							response->buffer);
			};

			// Forward this response sending task to HTTP listening thread
			if (listen_loop_->IsRunning()) {
				ENGINE_LOG_TRACE(engine::GetLogger(), "this={} dispatch this SendReply to listening thread", (void*)this);
				listen_loop_->RunInLoop(f);
			} else {
				ENGINE_LOG_WARN(engine::GetLogger(), "this={} listening thread is going to stop. we discards this request.", (void*)this);
				// TODO do we need do some resource recycling about the evhttp_request?
			}
		}
	}
}
