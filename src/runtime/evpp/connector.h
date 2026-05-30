#pragma once

#include <algorithm>
#include <vector>

#include "runtime/evpp/duration.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/invoke_timer.h"

namespace evpp {
class EventLoop;
class FdChannel;
class TimerEventWatcher;
class DNSResolver;
class TCPClient;

struct ConnectorConfig {
	int max_retries = 5;
	int retry_interval_ms = 1000;
	int max_retry_interval_ms = 30000;
	double backoff_multiplier = 2.0;
};

class CLOUD_ENGINE_API Connector : public std::enable_shared_from_this<Connector> {
	public:
	typedef std::function<void(evpp_socket_t sockfd, const std::string& /*local addr*/)>
		NewConnectionCallback;
	Connector(EventLoop* loop, TCPClient* client);
	~Connector();
	void Start();
	void Cancel();

	public:
	void SetNewConnectionCallback(NewConnectionCallback cb) {
		conn_fn_ = cb;
	}
	void SetRetryConfig(const ConnectorConfig& cfg) {
		retry_cfg_ = cfg;
		if (retry_cfg_.retry_interval_ms <= 0) retry_cfg_.retry_interval_ms = 1;
		if (retry_cfg_.max_retry_interval_ms < retry_cfg_.retry_interval_ms) {
			retry_cfg_.max_retry_interval_ms = retry_cfg_.retry_interval_ms;
		}
		if (retry_cfg_.backoff_multiplier < 1.0) retry_cfg_.backoff_multiplier = 1.0;
	}
	int retry_count() const { return retry_count_; }
	bool IsConnecting() const {
		return status_ == kConnecting;
	}
	bool IsConnected() const {
		return status_ == kConnected;
	}
	bool IsDisconnected() const {
		return status_ == kDisconnected;
	}
	int status() const {
		return status_;
	}

	private:
	void Connect();
	void HandleWrite();
	void HandleError();
	void OnConnectTimeout();
	void OnDNSResolved(const std::vector<struct in_addr>& addrs);
	std::string StatusToString() const;

	private:
	enum Status {
		kDisconnected,
		kDNSResolving,
		kDNSResolved,
		kConnecting,
		kConnected
	};
	std::atomic<Status> status_{kDisconnected};
	EventLoop* loop_;
	TCPClient* owner_tcp_client_;

	std::string remote_addr_;  // host:port
	std::string remote_host_;  // host
	int remote_port_ = 0;  // port
	struct sockaddr_storage raddr_;

	Duration timeout_;

	evpp_socket_t fd_ = -1;

	// A flag indicate whether the Connector owns this fd.
	// If the Connector owns this fd, the Connector has responsibility to close this fd.
	bool own_fd_ = false;

	std::unique_ptr<FdChannel> chan_;
	std::unique_ptr<TimerEventWatcher> timer_;
	std::shared_ptr<DNSResolver> dns_resolver_;
	InvokeTimerPtr reconnect_timer_;
	NewConnectionCallback conn_fn_;

	ConnectorConfig retry_cfg_;
	int retry_count_ = 0;
	int current_interval_ms_ = 0;
};
}
