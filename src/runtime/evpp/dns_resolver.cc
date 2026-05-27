#include "runtime/evpp/dns_resolver.h"

#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/event_watcher.h"
#include "runtime/evpp/libevent.h"

namespace evpp {
DNSResolver::DNSResolver(EventLoop* evloop,
						 const std::string& h,
						 Duration timeout,
						 const Functor& f)
	: loop_(evloop),
	  dnsbase_(nullptr),
	  dns_req_(nullptr),
	  host_(h),
	  timeout_(timeout),
	  functor_(f) {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} tid={} this={}",
					 (void*) this,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()),
					 (void*) this);
}

DNSResolver::~DNSResolver() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} tid={} this={}",
					 (void*) this,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()),
					 (void*) this);
	assert(dnsbase_ == nullptr);

#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	assert(!timer_);
#endif
	// If the DNS request was cancelled, the libevent callback won't fire,
	// so we must clean up the callback argument ourselves.
	if (evdns_cb_arg_) {
		delete evdns_cb_arg_;
		evdns_cb_arg_ = nullptr;
	}
}

void DNSResolver::Start() {
	auto f = [this]() {
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} tid={} this={}",
						 (void*) this,
						 std::hash<std::thread::id>{}(std::this_thread::get_id()),
						 (void*) this);
		assert(loop_->IsInLoopThread());

#if LIBEVENT_VERSION_NUMBER >= 0x02001500
		AsyncDNSResolve();
#else
		SyncDNSResolve();
#endif
	};
	loop_->RunInLoop(f);
}

void DNSResolver::SyncDNSResolve() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	/* Build the hints to tell getaddrinfo how to act. */
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* v4 or v6 is fine. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP; /* We want a TCP socket */
	hints.ai_flags = 0;

	/* Look up the hostname. */
	struct addrinfo* answer = nullptr;
	int err = getaddrinfo(host_.c_str(), nullptr, &hints, &answer);
	if (err != 0) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "this={} getaddrinfo failed. err={} {}",
						 (void*) this,
						 err,
						 gai_strerror(err));
	} else {
		for (struct addrinfo* rp = answer; rp != nullptr; rp = rp->ai_next) {
			struct sockaddr_in* a = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);

			if (a->sin_addr.s_addr == 0) {
				continue;
			}

			addrs_.push_back(a->sin_addr);
			ENGINE_LOG_TRACE(engine::GetLogger(),
							 "this={} host={} resolved a ip={}",
							 (void*) this,
							 host_,
							 inet_ntoa(a->sin_addr));
		}
	}
	if (answer) {
		evutil_freeaddrinfo(answer);
	}
	OnResolved();
}

void DNSResolver::Cancel() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);
	assert(loop_->IsInLoopThread());
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	if (dns_req_) {
		evdns_getaddrinfo_cancel(dns_req_);
		dns_req_ = nullptr;
	}
#endif
	if (evdns_cb_arg_) {
		delete evdns_cb_arg_;
		evdns_cb_arg_ = nullptr;
	}
	if (timer_) {
		timer_->Cancel();
		timer_.reset();
	}
	functor_ = Functor();  // Release the callback
}

void DNSResolver::AsyncWait() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} tid={} this={}",
					 (void*) this,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()),
					 (void*) this);
	timer_.reset(new TimerEventWatcher(loop_, std::bind(&DNSResolver::OnTimeout, this), timeout_));
	timer_->SetCancelCallback(std::bind(&DNSResolver::OnCanceled, this));
	timer_->Init();
	timer_->AsyncWait();
}

void DNSResolver::OnTimeout() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} tid={} this={}",
					 (void*) this,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()),
					 (void*) this);
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	evdns_getaddrinfo_cancel(dns_req_);
	dns_req_ = nullptr;
#endif
	if (evdns_cb_arg_) {
		delete evdns_cb_arg_;
		evdns_cb_arg_ = nullptr;
	}
	ClearTimer();
	OnResolved();
}

void DNSResolver::OnCanceled() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} tid={} this={}",
					 (void*) this,
					 std::hash<std::thread::id>{}(std::this_thread::get_id()),
					 (void*) this);
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
	if (dns_req_) {
		evdns_getaddrinfo_cancel(dns_req_);
		dns_req_ = nullptr;
	}
#endif
	if (evdns_cb_arg_) {
		delete evdns_cb_arg_;
		evdns_cb_arg_ = nullptr;
	}
}


#if LIBEVENT_VERSION_NUMBER >= 0x02001500
void DNSResolver::AsyncDNSResolve() {
	ENGINE_LOG_TRACE(engine::GetLogger(), "this={}", (void*) this);

	// Set a timer to watch the DNS resolving
	AsyncWait();

	/* Build the hints to tell getaddrinfo how to act. */
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* v4 or v6 is fine. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP; /* We want a TCP socket */
	hints.ai_flags = 0;


	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} call shared_from_this", (void*) this);
	std::shared_ptr<DNSResolver> p = shared_from_this();
	evdns_cb_arg_ = new std::shared_ptr<DNSResolver>(p);
	dnsbase_ = evdns_base_new(loop_->event_base(), 1);
	assert(dnsbase_);
	dns_req_ = evdns_getaddrinfo(dnsbase_,
								 host_.c_str(),
								 nullptr /* no service name given */
								 ,
								 &hints,
								 &DNSResolver::OnResolved,
								 evdns_cb_arg_);
	if (!dns_req_) {
		ENGINE_LOG_ERROR(engine::GetLogger(), "evdns_getaddrinfo failed.");
		delete evdns_cb_arg_;
		evdns_cb_arg_ = nullptr;
		evdns_base_free(dnsbase_, 0);
		dnsbase_ = nullptr;
		ClearTimer();
		OnResolved();
		return;
	}
}

void DNSResolver::OnResolved(int errcode, struct addrinfo* addr) {
	if (errcode != 0) {
		if (errcode != EVUTIL_EAI_CANCEL) {
			ClearTimer();
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "DNS resolve failed, error code: {}, error msg: {}",
							 errcode,
							 evutil_gai_strerror(errcode));
		} else {
			ENGINE_LOG_WARN(
				engine::GetLogger(), "this={} DNS resolve cancel, may be timeout", (void*) this);
		}

		ENGINE_LOG_WARN(engine::GetLogger(),
						"this={} delete DNS base. errcode={} {}",
						(void*) this,
						errcode,
						strerror(errcode));
		evdns_base_free(dnsbase_, 0);
		dnsbase_ = nullptr;
		OnResolved();
		return;
	}


	if (addr == nullptr) {
		ENGINE_LOG_ERROR(engine::GetLogger(),
						 "this={} dns resolve error, addr can not be nullptr",
						 (void*) this);

		ENGINE_LOG_TRACE(engine::GetLogger(), "this={} delete dns base", (void*) this);
		evdns_base_free(dnsbase_, 0);
		dnsbase_ = nullptr;
		ClearTimer();
		OnResolved();
		return;
	}


	if (addr->ai_canonname) {
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} resolve canon name: {}",
						 (void*) this,
						 addr->ai_canonname);
	}

	for (struct addrinfo* rp = addr; rp != nullptr; rp = rp->ai_next) {
		struct sockaddr_in* a = sock::sockaddr_in_cast(rp->ai_addr);

		if (a->sin_addr.s_addr == 0) {
			continue;
		}

		addrs_.push_back(a->sin_addr);
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} host={} resolved a ip={}",
						 (void*) this,
						 host_,
						 inet_ntoa(a->sin_addr));
	}
	evutil_freeaddrinfo(addr);
	ClearTimer();

	ENGINE_LOG_TRACE(engine::GetLogger(), "this={} delete DNS base", (void*) this);
	evdns_base_free(dnsbase_, 0);  //TODO Do we need to free dns_req_?
	dnsbase_ = nullptr;
	OnResolved();
}

void DNSResolver::OnResolved(int errcode, struct addrinfo* addr, void* arg) {
	std::shared_ptr<DNSResolver>* pp = reinterpret_cast<std::shared_ptr<DNSResolver>*>(arg);
	ENGINE_LOG_TRACE(engine::GetLogger(), "this->use_count={}", pp->use_count());
	// Null the member BEFORE invoking the callback — Cancel() may be called
	// from within the callback chain, and must not double-delete pp.
	(*pp)->evdns_cb_arg_ = nullptr;
	(*pp)->OnResolved(errcode, addr);
	delete pp;
}
#endif

void DNSResolver::OnResolved() {
	if (functor_) {
		functor_(addrs_);

		// Release the callback immediately.
		// Sometimes, when it is timeout, this callback will be invoked in OnTimeout()
		// and `evdns_getaddrinfo_cancel(dns_req_)` will also invoke
		// OnResolved in next loop time. So we need to release this callback.
		functor_ = Functor();
	}
}

void DNSResolver::ClearTimer() {
	if (!timer_) return;
	timer_->SetCancelCallback(TimerEventWatcher::Handler());
	timer_->Cancel();
	timer_.reset();
}

}
