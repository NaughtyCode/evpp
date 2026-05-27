#pragma once

#include "runtime/evpp/duration.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/sys_addrinfo.h"

struct evdns_base;
struct evdns_getaddrinfo_request;
namespace evpp {
class EventLoop;
class TimerEventWatcher;
class EVPP_EXPORT DNSResolver : public std::enable_shared_from_this<DNSResolver> {
	public:
	//TODO IPv6 DNS resolver
	typedef std::function<void(const std::vector<struct in_addr>& addrs)> Functor;

	DNSResolver(EventLoop* evloop, const std::string& host, Duration timeout, const Functor& f);
	~DNSResolver();
	void Start();
	void Cancel();
	const std::string& host() const {
		return host_;
	}

	private:
	void SyncDNSResolve();
	void AsyncDNSResolve();
	void AsyncWait();
	void OnTimeout();
	void OnCanceled();
	void ClearTimer();
	void OnResolved(int errcode, struct addrinfo* addr);
	void OnResolved();
	static void OnResolved(int errcode, struct addrinfo* addr, void* arg);

	private:
	EventLoop* loop_ = nullptr;
	struct evdns_base* dnsbase_ = nullptr;
	struct evdns_getaddrinfo_request* dns_req_ = nullptr;
	std::string host_;
	Duration timeout_;
	Functor functor_;
	std::unique_ptr<TimerEventWatcher> timer_;
	std::vector<struct in_addr> addrs_;
	// Owned by libevent callback; must be deleted manually on cancel/timeout
	// since evdns_getaddrinfo_cancel prevents the callback from firing.
	std::shared_ptr<DNSResolver>* evdns_cb_arg_ = nullptr;
};

}
