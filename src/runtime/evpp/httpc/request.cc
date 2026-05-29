#include "runtime/evpp/httpc/request.h"

#include "runtime/evpp/httpc/conn_pool.h"
#include "runtime/evpp/httpc/response.h"
#include "runtime/evpp/httpc/url_parser.h"
#include "runtime/evpp/libevent.h"

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
#include <openssl/err.h>
#endif

namespace evpp {
namespace httpc {
const std::string Request::empty_ = "";

Request::Request(ConnPool* pool,
				 EventLoop* loop,
				 const std::string& http_uri,
				 const std::string& body)
	: pool_(pool),
	  loop_(loop),
	  host_(pool->host()),
	  port_(pool->port()),
	  uri_(http_uri),
	  body_(body) {
}

Request::Request(EventLoop* loop,
				 const std::string& http_url,
				 const std::string& body,
				 Duration timeout)
	: pool_(nullptr), loop_(loop), body_(body) {
	// evhttp_uri_parse is the canonical libevent URI parser; a custom
	// parser would save one allocation but offer no measurable perf gain.
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	struct evhttp_uri* evuri = evhttp_uri_parse(http_url.c_str());
	if (!evuri) {
		port_ = 80;
		uri_ = "/";
		host_ = http_url;
#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
		conn_.reset(CLOUDENGINE_MEM_NEW(Conn, loop, host_, port_, false, timeout));
#else
		conn_.reset(CLOUDENGINE_MEM_NEW(Conn, loop, host_, port_, timeout));
#endif
		return;
	}
	uri_ = evhttp_uri_get_path(evuri);
	if (uri_[0] == 0) {
		uri_ = "/";
	}
	const char* query = evhttp_uri_get_query(evuri);
	if (query && strlen(query) > 0) {
		uri_ += "?";
		uri_ += query;
	}

	host_ = evhttp_uri_get_host(evuri);

	port_ = evhttp_uri_get_port(evuri);

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
	const char* scheme = evhttp_uri_get_scheme(evuri);
	bool enable_ssl = scheme && strcasecmp(scheme, "https") == 0;
	if (port_ < 0) {
		port_ = enable_ssl ? 443 : 80;
	}
	conn_.reset(CLOUDENGINE_MEM_NEW(Conn, loop, host_, port_, enable_ssl, timeout));
#else
	if (port_ < 0) {
		port_ = 80;
	}
	conn_.reset(CLOUDENGINE_MEM_NEW(Conn, loop, host_, port_, timeout));
#endif
	evhttp_uri_free(evuri);
#else
	URLParser p(http_url);
	conn_.reset(CLOUDENGINE_MEM_NEW(Conn, loop, p.host, p.port, timeout));
	if (p.query.empty()) {
		uri_ = p.path;
	} else {
		uri_ = p.path + "?" + p.query;
	}
	host_ = p.host;
	port_ = p.port;
#endif
}

Request::~Request() {
	assert(loop_->IsInLoopThread());
}

void Request::Execute(const Handler& h) {
	handler_ = h;
	loop_->RunInLoop(std::bind(&Request::ExecuteInLoop, this));
}

void Request::ExecuteInLoop() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(loop_->IsInLoopThread());
	evhttp_cmd_type req_type = EVHTTP_REQ_GET;

	std::string errmsg;
	struct evhttp_request* req = nullptr;

	if (conn_) {
		assert(pool_ == nullptr);
		if (!conn_->Init()) {
			errmsg = "conn init fail";
			goto failed;
		}
	} else {
		assert(pool_);
		conn_ = pool_->Get(loop_);
		if (!conn_->Init()) {
			errmsg = "conn init fail";
			goto failed;
		}
	}

	req = evhttp_request_new(&Request::HandleResponse, this);
	if (!req) {
		errmsg = "evhttp_request_new fail";
		goto failed;
	}

	if (evhttp_add_header(req->output_headers, "host", conn_->host().c_str())) {
		evhttp_request_free(req);
		errmsg = "evhttp_add_header failed";
		goto failed;
	}

	for (const auto& header : headers_) {
		if (evhttp_add_header(req->output_headers, header.first.c_str(), header.second.c_str())) {
			evhttp_request_free(req);
			errmsg = "evhttp_add_header failed";
			goto failed;
		}
	}

	if (!body_.empty()) {
		req_type = EVHTTP_REQ_POST;
		if (evbuffer_add(req->output_buffer, body_.c_str(), body_.size())) {
			evhttp_request_free(req);
			errmsg = "evbuffer_add fail";
			goto failed;
		}
	}

	if (evhttp_make_request(conn_->evhttp_conn(), req, req_type, uri_.c_str()) != 0) {
		// evhttp_make_request only takes ownership on success (return 0).
		// On failure (return -1) the caller must free the request.
		evhttp_request_free(req);
		errmsg = "evhttp_make_request fail";
		goto failed;
	}

	return;

failed:
	// Retry
	if (retried_ < retry_number_) {
		ENGINE_LOG_WARN(engine::GetLogger(),
						"this={} http request failed : {} retried={} max retry_time={}. Try again.",
						(void*) this,
						errmsg,
						retried_,
						retry_number_);
		Retry();
		return;
	}

	// Return the connection to pool if we got it from pool and retries exhausted
	if (pool_ && conn_) {
		pool_->Put(conn_);
		conn_.reset();
	}

	std::shared_ptr<Response> response(CLOUDENGINE_MEM_NEW(Response, this, nullptr));
	handler_(response);
}

void Request::AddHeader(const std::string& header, const std::string& value) {
	headers_[header] = value;
}

void Request::Retry() {
	retried_ += 1;

	// Recycling the http Connection object for retry.
	// Connection will be obtained again by ExecuteInLoop
	if (pool_) {
		pool_->Put(conn_);
		conn_.reset();
	}

	if (retry_interval_.IsZero()) {
		ExecuteInLoop();
	} else {
		loop_->RunAfter(retry_interval_, std::bind(&Request::ExecuteInLoop, this));
	}
}

void Request::HandleResponse(struct evhttp_request* r, void* v) {
	Request* thiz = (Request*) v;
	assert(thiz);
	thiz->HandleResponse(r);
}

void Request::HandleResponse(struct evhttp_request* r) {
	assert(loop_->IsInLoopThread());

	if (r) {
		int response_code = r->response_code;
		bool needs_retry = response_code >= 500 && response_code < 600;
		if (!needs_retry || retried_ >= retry_number_) {
			ENGINE_LOG_WARN(engine::GetLogger(),
							"this={} response_code={} retried={} max retry_time={}",
							(void*) this,
							r->response_code,
							retried_,
							retry_number_);
			std::shared_ptr<Response> response(CLOUDENGINE_MEM_NEW(Response, this, r));

			//Recycling the http Connection object
			if (pool_) {
				pool_->Put(conn_);
				conn_.reset();
			}

			handler_(response);
			return;
		}
	}

	// Retry
	if (retried_ < retry_number_) {
		ENGINE_LOG_WARN(engine::GetLogger(),
						"this={} response_code={} retried={} max retry_time={}. Try again",
						(void*) this,
						(r ? r->response_code : 0),
						retried_,
						retry_number_);
		Retry();
		return;
	}

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL)
	if (!r) {
		int errcode = EVUTIL_SOCKET_ERROR();
		unsigned long oslerr;
		bool printed_some_error = false;
		char buffer[256];
	#ifdef EVENT__HAVE_OPENSSL
	while ((oslerr = bufferevent_get_openssl_error(conn_->bufferevent()))) {
#else
	while (0) {
	unsigned long oslerr = 0;
	(void)oslerr;
#endif
			ERR_error_string_n(oslerr, buffer, sizeof(buffer));
			ENGINE_LOG_ERROR(engine::GetLogger(), "Openssl error: {}", buffer);
			printed_some_error = true;
		}
		if (!printed_some_error) {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "socket error({}): {}",
							 errcode,
							 evutil_socket_error_to_string(errcode));
		}
	}
#endif
	// Eventually this Request failed
	std::shared_ptr<Response> response(CLOUDENGINE_MEM_NEW(Response, this, r));

	// Recycling the http Connection object
	if (pool_) {
		pool_->Put(conn_);
		conn_.reset();
	}

	handler_(response);
}

}  // httpc
}  // evpp
